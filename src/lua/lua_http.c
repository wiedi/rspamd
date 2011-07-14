/* Copyright (c) 2010, Vsevolod Stakhov
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *       * Redistributions of source code must retain the above copyright
 *         notice, this list of conditions and the following disclaimer.
 *       * Redistributions in binary form must reproduce the above copyright
 *         notice, this list of conditions and the following disclaimer in the
 *         documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Rambler BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "lua_common.h"
#include "../buffer.h"
#include "../dns.h"

#define MAX_HEADERS_SIZE 8192

LUA_FUNCTION_DEF (http, make_post_request);
LUA_FUNCTION_DEF (http, make_get_request);

static const struct luaL_reg    httplib_m[] = {
	LUA_INTERFACE_DEF (http, make_post_request),
	LUA_INTERFACE_DEF (http, make_get_request),
	{"__tostring", lua_class_tostring},
	{NULL, NULL}
};

struct lua_http_header {
	gchar *name;
	gchar *value;
};

struct lua_http_ud {
	gint parser_state;
	struct worker_task *task;
	lua_State *L;
	const gchar *callback;
	gchar *req_buf;
	gint req_len;
	gint port;
	gint timeout;
	gint code;
	gint fd;
	rspamd_io_dispatcher_t *io_dispatcher;
	gint rep_len;
	GList *headers;
};

static struct worker_task      *
lua_check_task (lua_State * L)
{
	void                           *ud = luaL_checkudata (L, 1, "rspamd{task}");
	luaL_argcheck (L, ud != NULL, 1, "'task' expected");
	return *((struct worker_task **)ud);
}

static void
lua_http_fin (void *arg)
{
	struct lua_http_ud             *ud = arg;

	rspamd_remove_dispatcher (ud->io_dispatcher);
	close (ud->fd);
}

static void
lua_http_push_error (gint code, struct lua_http_ud *ud)
{
	struct worker_task            **ptask;

	/* Push error */
	lua_getglobal (ud->L, ud->callback);
	ptask = lua_newuserdata (ud->L, sizeof (struct worker_task *));
	lua_setclass (ud->L, "rspamd{task}", -1);

	*ptask = ud->task;
	/* Code */
	lua_pushnumber (ud->L, code);
	/* Headers */
	lua_pushnil (ud->L);
	/* Reply */
	lua_pushnil (ud->L);
	if (lua_pcall (ud->L, 4, 0, 0) != 0) {
		msg_info ("call to %s failed: %s", ud->callback, lua_tostring (ud->L, -1));
	}

	if (ud->headers != NULL) {
		g_list_free (ud->headers);
		ud->headers = NULL;
	}

	ud->parser_state = 3;
	remove_normal_event (ud->task->s, lua_http_fin, ud);

	ud->task->save.saved--;
	if (ud->task->save.saved == 0) {
		/* Call other filters */
		ud->task->save.saved = 1;
		process_filters (ud->task);
	}
}

static void
lua_http_push_reply (f_str_t *in, struct lua_http_ud *ud)
{
	GList                          *cur;
	struct lua_http_header         *header;
	struct worker_task            **ptask;

	/* Push error */
	lua_getglobal (ud->L, ud->callback);
	ptask = lua_newuserdata (ud->L, sizeof (struct worker_task *));
	lua_setclass (ud->L, "rspamd{task}", -1);

	*ptask = ud->task;
	/* Code */
	lua_pushnumber (ud->L, ud->code);
	/* Headers */
	lua_newtable (ud->L);
	cur = ud->headers;

	while (cur) {
		header = cur->data;
		lua_pushstring (ud->L, header->name);
		lua_pushstring (ud->L, header->value);
		lua_settable (ud->L, -3);
		cur = g_list_next (cur);
	}
	/* Reply */
	lua_pushlstring (ud->L, in->begin, in->len);

	if (lua_pcall (ud->L, 4, 0, 0) != 0) {
		msg_info ("call to %s failed: %s", ud->callback, lua_tostring (ud->L, -1));
	}

	if (ud->headers != NULL) {
		g_list_free (ud->headers);
		ud->headers = NULL;
	}

	remove_normal_event (ud->task->s, lua_http_fin, ud);
	ud->task->save.saved--;
	if (ud->task->save.saved == 0) {
		/* Call other filters */
		ud->task->save.saved = 1;
		process_filters (ud->task);
	}
}

/*
 * Parsing utils
 */
static gboolean
lua_http_parse_first_line (struct lua_http_ud *ud, f_str_t *in)
{
	const gchar                    *p;

	/* Assume first line is like this: HTTP/1.1 200 OK */
	if (in->len < sizeof ("HTTP/1.1 OK") + 2) {
		msg_info ("bad http string: %V", in);
		return FALSE;
	}

	p = in->begin + sizeof("HTTP/1.1 ") - 1;
	ud->code = strtoul (p, NULL, 10);

	ud->parser_state = 1;
	return TRUE;
}

static gboolean
lua_http_parse_header_line (struct lua_http_ud *ud, f_str_t *in)
{
	const gchar                    *p = in->begin;
	struct lua_http_header         *new;

	while (p < in->begin + in->len) {
		if (*p == ':') {
			break;
		}
		p ++;
	}

	if (*p != ':') {
		return FALSE;
	}
	/* Copy name */
	new = memory_pool_alloc (ud->task->task_pool, sizeof (struct lua_http_header));
	new->name = memory_pool_alloc (ud->task->task_pool, p - in->begin + 1);
	rspamd_strlcpy (new->name, in->begin, p - in->begin + 1);

	p ++;
	/* Copy value */
	while (p < in->begin + in->len && g_ascii_isspace (*p)) {
		p ++;
	}
	new->value = memory_pool_alloc (ud->task->task_pool, in->begin + in->len - p + 1);
	rspamd_strlcpy (new->value, p, in->begin + in->len - p + 1);

	/* Check content-length */
	if (ud->rep_len == 0 && g_ascii_strcasecmp (new->name, "content-length") == 0) {
		ud->rep_len = strtoul (new->value, NULL, 10);
	}

	/* Insert a header to the list */
	ud->headers = g_list_prepend (ud->headers, new);

	return TRUE;
}

/* Read callback */
static gboolean
lua_http_read_cb (f_str_t * in, void *arg)
{
	struct lua_http_ud             *ud = arg;

	switch (ud->parser_state) {
	case 0:
		/* Parse first line */
		return lua_http_parse_first_line (ud, in);
	case 1:
		if (ud->code != 200) {
			lua_http_push_error (ud->code, ud);
			return FALSE;
		}
		/* Parse header */
		if (in->len == 0) {
			/* Final line */
			if (ud->rep_len == 0) {
				/* No content-length */
				msg_info ("http reply contains no content-length header");
				lua_http_push_error (450, ud);
				return FALSE;
			}
			else {
				ud->parser_state = 2;
				rspamd_set_dispatcher_policy (ud->io_dispatcher, BUFFER_CHARACTER, ud->rep_len);
			}
		}
		else {
			return lua_http_parse_header_line (ud, in);
		}
		break;
	case 2:
		/* Get reply */
		lua_http_push_reply (in, ud);
		return FALSE;
	}

	return TRUE;
}

static void
lua_http_err_cb (GError * err, void *arg)
{
	struct lua_http_ud             *ud = arg;
	msg_info ("abnormally closing connection to http server error: %s", err->message);
	g_error_free (err);

	if (ud->parser_state != 3) {
		lua_http_push_error (500, ud);
	}
	else {
		remove_normal_event (ud->task->s, lua_http_fin, ud);
	}
}



static void
lua_http_dns_callback (struct rspamd_dns_reply *reply, gpointer arg)
{
	struct lua_http_ud             *ud = arg;
	union rspamd_reply_element     *elt;
	struct in_addr                  ina;
	struct timeval                  tv;

	if (reply->code != DNS_RC_NOERROR) {
		lua_http_push_error (450, ud);
		return;
	}

	/* Create socket to server */
	elt = reply->elements->data;
	memcpy (&ina, &elt->a.addr[0], sizeof (struct in_addr));

	ud->fd = make_tcp_socket (&ina, ud->port, FALSE, TRUE);

	if (ud->fd == -1) {
		lua_http_push_error (450, ud);
		return;
	}

	/* Create dispatcher for HTTP protocol */
	msec_to_tv (ud->timeout, &tv);
	ud->io_dispatcher = rspamd_create_dispatcher (ud->fd, BUFFER_LINE, lua_http_read_cb, NULL, lua_http_err_cb,
			&tv, ud);
	/* Write request */
	register_async_event (ud->task->s, lua_http_fin, ud, FALSE);

	if (!rspamd_dispatcher_write (ud->io_dispatcher, ud->req_buf, ud->req_len, TRUE, TRUE)) {
		lua_http_push_error (450, ud);
		return;
	}
}

/**
 * Common request function
 */
static gint
lua_http_make_request_common (lua_State *L, struct worker_task *task, const gchar *callback,
		const gchar *hostname, const gchar *path, const gchar *data, gint top)
{
	gint                           r, s, datalen;
	struct lua_http_ud			  *ud;

	/* Calculate buffer size */
	datalen = (data != NULL) ? strlen (data) : 0;
	s = MAX_HEADERS_SIZE + sizeof (CRLF) * 3 + strlen (hostname) + strlen (path) + datalen
			+ sizeof ("POST HTTP/1.1");

	ud = memory_pool_alloc0 (task->task_pool, sizeof (struct lua_http_ud));
	ud->L = L;
	ud->task = task;
	/* Preallocate buffer */
	ud->req_buf = memory_pool_alloc (task->task_pool, s);
	ud->callback = callback;

	/* Print request */
	r = rspamd_snprintf (ud->req_buf, s, "%s %s HTTP/1.1" CRLF
										 "Connection: close" CRLF
										 "Host: %s" CRLF,
										 (data != NULL) ? "POST" : "GET", path, hostname);
	if (datalen > 0) {
		r += rspamd_snprintf (ud->req_buf + r, s - r, "Content-Length: %d" CRLF, datalen);
	}
	/* Now assume that we have a table with headers at the top of the stack */

	if (lua_gettop (L) > top && lua_istable (L, top + 1)) {
		/* Add headers */
		lua_pushnil (L);  /* first key */
		while (lua_next (L, top + 1) != 0) {
			r += rspamd_snprintf (ud->req_buf + r, s - r, "%s: %s" CRLF, lua_tostring (L, -2), lua_tostring (L, -1));
			lua_pop(L, 1);
		}
	}
	/* Now check port and timeout */
	if (lua_gettop (L) > top + 1) {
		ud->port = lua_tonumber (L, top + 2);
	}
	else {
		ud->port = 80;
	}
	if (lua_gettop (L) > top + 2) {
		ud->timeout = lua_tonumber (L, top + 3);
	}
	else {
		/* Assume default timeout as 1000 msec */
		ud->timeout = 1000;
	}

	if (datalen > 0) {
		r += rspamd_snprintf (ud->req_buf + r, s - r, CRLF "%s", data);
	}
	else {
		r += rspamd_snprintf (ud->req_buf + r, s - r, CRLF);
	}

	ud->req_len = r;

	/* Resolve hostname */
	if (make_dns_request (task->resolver, task->s, task->task_pool, lua_http_dns_callback, ud,
			DNS_REQUEST_A, hostname)) {
		task->dns_requests ++;
		task->save.saved++;
	}

	return 0;
}

/*
 * Typical usage:
 * rspamd_http.post_request(task, 'callback', 'hostname', 'path', 'data'[, headers -> { name = 'value' }])
 */
static gint
lua_http_make_post_request (lua_State *L)
{
	struct worker_task            *task = lua_check_task (L);
	const gchar                   *hostname, *path, *data, *callback;

	/* Now extract hostname, path and data */

	callback = memory_pool_strdup (task->task_pool, luaL_checkstring (L, 2));
	hostname = memory_pool_strdup (task->task_pool, luaL_checkstring (L, 3));
	path = memory_pool_strdup (task->task_pool, luaL_checkstring (L, 4));
	data = memory_pool_strdup (task->task_pool, luaL_checkstring (L, 5));

	if (callback != NULL && hostname != NULL && path != NULL && data != NULL) {
		return lua_http_make_request_common (L, task, callback, hostname, path, data, 5);
	}
	else {
		msg_info ("invalid arguments number");
	}

	return 0;
}

/*
 * Typical usage:
 * rspamd_http.get_request(task, 'callback', 'hostname', 'path'[, headers -> { name = 'value' }])
 */
static gint
lua_http_make_get_request (lua_State *L)
{
	struct worker_task            *task = lua_check_task (L);
	const gchar                   *hostname, *path, *callback;

	/* Now extract hostname, path and data */

	callback = memory_pool_strdup (task->task_pool, luaL_checkstring (L, 2));
	hostname = memory_pool_strdup (task->task_pool, luaL_checkstring (L, 3));
	path = memory_pool_strdup (task->task_pool, luaL_checkstring (L, 4));

	if (callback != NULL && hostname != NULL && path != NULL) {
		return lua_http_make_request_common (L, task, callback, hostname, path, NULL, 4);
	}
	else {
		msg_info ("invalid arguments number");
	}

	return 0;
}

gint
luaopen_http (lua_State * L)
{

	luaL_openlib (L, "rspamd_http", httplib_m, 0);

	return 1;
}