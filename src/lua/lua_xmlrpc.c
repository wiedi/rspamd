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


LUA_FUNCTION_DEF (xmlrpc, parse_reply);

static const struct luaL_reg    xmlrpclib_m[] = {
	LUA_INTERFACE_DEF (xmlrpc, parse_reply),
	{"__tostring", lua_class_tostring},
	{NULL, NULL}
};

struct lua_xmlrpc_ud {
	gint parser_state;
	gint depth;
	gint param_count;
	lua_State *L;
};

static void xmlrpc_start_element (GMarkupParseContext *context, const gchar *name, const gchar **attribute_names,
								const gchar **attribute_values, gpointer user_data, GError **error);
static void xmlrpc_end_element (GMarkupParseContext	*context, const gchar *element_name, gpointer user_data,
								GError **error);
static void xmlrpc_error (GMarkupParseContext *context, GError *error, gpointer user_data);
static void xmlrpc_text (GMarkupParseContext *context, const gchar *text, gsize text_len, gpointer user_data,
								GError **error);

static GMarkupParser xmlrpc_parser = {
	.start_element = xmlrpc_start_element,
	.end_element = xmlrpc_end_element,
	.passthrough = NULL,
	.text = xmlrpc_text,
	.error = xmlrpc_error,
};

static GQuark
xmlrpc_error_quark (void)
{
	return g_quark_from_static_string ("xmlrpc-error-quark");
}

static void
xmlrpc_start_element (GMarkupParseContext *context, const gchar *name, const gchar **attribute_names,
								const gchar **attribute_values, gpointer user_data, GError **error)
{
	struct lua_xmlrpc_ud           *ud = user_data;
	int                             last_state;

	last_state = ud->parser_state;

	switch (ud->parser_state) {
	case 0:
		/* Expect tag methodResponse */
		if (g_ascii_strcasecmp (name, "methodResponse") == 0) {
			ud->parser_state = 1;
		}
		else {
			/* Error state */
			ud->parser_state = 99;
		}
		break;
	case 1:
		/* Expect tag params */
		if (g_ascii_strcasecmp (name, "params") == 0) {
			ud->parser_state = 2;
			/* result -> table of params indexed by int */
			lua_newtable (ud->L);
		}
		else {
			/* Error state */
			ud->parser_state = 99;
		}
		break;
	case 2:
		/* Expect tag param */
		if (g_ascii_strcasecmp (name, "param") == 0) {
			ud->parser_state = 3;
			/* Create new param */
		}
		else {
			/* Error state */
			ud->parser_state = 99;
		}
		break;
	case 3:
		/* Expect tag value */
		if (g_ascii_strcasecmp (name, "value") == 0) {
			ud->parser_state = 4;
		}
		else {
			/* Error state */
			ud->parser_state = 99;
		}
		break;
	case 4:
		/* Expect tag struct */
		if (g_ascii_strcasecmp (name, "struct") == 0) {
			ud->parser_state = 5;
			/* Create new param of table type */
			lua_newtable (ud->L);
			ud->depth ++;
		}
		else if (g_ascii_strcasecmp (name, "string") == 0) {
			ud->parser_state = 11;
		}
		else if (g_ascii_strcasecmp (name, "int") == 0) {
			ud->parser_state = 12;
		}
		else {
			/* Error state */
			ud->parser_state = 99;
		}
		break;
	case 5:
		/* Parse structure */
		/* Expect tag member */
		if (g_ascii_strcasecmp (name, "member") == 0) {
			ud->parser_state = 6;
		}
		else {
			/* Error state */
			ud->parser_state = 99;
		}
		break;
	case 6:
		/* Expect tag name */
		if (g_ascii_strcasecmp (name, "name") == 0) {
			ud->parser_state = 7;
		}
		else {
			/* Error state */
			ud->parser_state = 99;
		}
		break;
	case 7:
		/* Accept value */
		if (g_ascii_strcasecmp (name, "value") == 0) {
			ud->parser_state = 8;
		}
		else {
			/* Error state */
			ud->parser_state = 99;
		}
		break;
	case 8:
		/* Parse any values */
		/* Primitives */
		if (g_ascii_strcasecmp (name, "string") == 0) {
			ud->parser_state = 11;
		}
		else if (g_ascii_strcasecmp (name, "int") == 0) {
			ud->parser_state = 12;
		}
		/* Structure */
		else if (g_ascii_strcasecmp (name, "struct") == 0) {
			ud->parser_state = 5;
			/* Create new param of table type */
			lua_newtable (ud->L);
			ud->depth ++;
		}
		else {
			/* Error state */
			ud->parser_state = 99;
		}
		break;
	}

	if (ud->parser_state == 99) {
		g_set_error (error, xmlrpc_error_quark(), 1, "xmlrpc parse error on state: %d, while parsing start tag: %s",
				last_state, name);
	}
}

static void
xmlrpc_end_element (GMarkupParseContext	*context, const gchar *name, gpointer user_data, GError **error)
{
	struct lua_xmlrpc_ud           *ud = user_data;
	int                             last_state;

	last_state = ud->parser_state;

	switch (ud->parser_state) {
	case 0:
		ud->parser_state = 99;
		break;
	case 1:
		/* Got methodResponse */
		if (g_ascii_strcasecmp (name, "methodResponse") == 0) {
			/* End processing */
			ud->parser_state = 100;
		}
		else {
			/* Error state */
			ud->parser_state = 99;
		}
		break;
	case 2:
		/* Got tag params */
		if (g_ascii_strcasecmp (name, "params") == 0) {
			ud->parser_state = 1;
		}
		else {
			/* Error state */
			ud->parser_state = 99;
		}
		break;
	case 3:
		/* Got tag param */
		if (g_ascii_strcasecmp (name, "param") == 0) {
			ud->parser_state = 2;
			lua_rawseti (ud->L, -2, ++ud->param_count);
		}
		else {
			/* Error state */
			ud->parser_state = 99;
		}
		break;
	case 4:
		/* Got tag value */
		if (g_ascii_strcasecmp (name, "value") == 0) {
			if (ud->depth == 0) {
				ud->parser_state = 3;
			}
			else {
				/* Parse other members */
				ud->parser_state = 6;
			}
		}
		else {
			/* Error state */
			ud->parser_state = 99;
		}
		break;
	case 5:
		/* Got tag struct */
		if (g_ascii_strcasecmp (name, "struct") == 0) {
			ud->parser_state = 4;
			ud->depth --;
		}
		else {
			/* Error state */
			ud->parser_state = 99;
		}
		break;
	case 6:
		/* Got tag member */
		if (g_ascii_strcasecmp (name, "member") == 0) {
			ud->parser_state = 5;
			/* Set table */
			lua_settable (ud->L, -3);
		}
		else {
			/* Error state */
			ud->parser_state = 99;
		}
		break;
	case 7:
		/* Got tag name */
		if (g_ascii_strcasecmp (name, "name") == 0) {
			ud->parser_state = 7;
		}
		else {
			/* Error state */
			ud->parser_state = 99;
		}
		break;
	case 8:
		/* Got tag value */
		if (g_ascii_strcasecmp (name, "value") == 0) {
			ud->parser_state = 6;
		}
		else {
			/* Error state */
			ud->parser_state = 99;
		}
		break;
	case 11:
	case 12:
		/* Parse any values */
		/* Primitives */
		if (g_ascii_strcasecmp (name, "string") == 0) {
			ud->parser_state = 8;
		}
		else if (g_ascii_strcasecmp (name, "int") == 0) {
			ud->parser_state = 8;
		}
		else {
			/* Error state */
			ud->parser_state = 99;
		}
		break;
	}

	if (ud->parser_state == 99) {
		g_set_error (error, xmlrpc_error_quark(), 1, "xmlrpc parse error on state: %d, while parsing end tag: %s",
				last_state, name);
	}
}

static void
xmlrpc_text (GMarkupParseContext *context, const gchar *text, gsize text_len, gpointer user_data, GError **error)
{
	struct lua_xmlrpc_ud           *ud = user_data;
	gint                            num;

	/* Strip line */
	while (g_ascii_isspace (*text) && text_len > 0) {
		text ++;
		text_len --;
	}
	while (g_ascii_isspace (text[text_len - 1]) && text_len > 0) {
		text_len --;
	}

	if (text_len > 0) {

		switch (ud->parser_state) {
		case 7:
			/* Push key */
			lua_pushlstring (ud->L, text, text_len);
			break;
		case 11:
			/* Push string value */
			lua_pushlstring (ud->L, text, text_len);
			break;
		case 12:
			/* Push integer value */
			num = strtoul (text, NULL, 10);
			lua_pushinteger (ud->L, num);
			break;
		}
	}
}

static void
xmlrpc_error (GMarkupParseContext *context, GError *error, gpointer user_data)
{
	struct lua_xmlrpc_ud           *ud = user_data;

	msg_err ("xmlrpc parser error: %s", error->message, ud->parser_state);
}

gint
lua_xmlrpc_parse_reply (lua_State *L)
{
	const gchar                    *data;
	GMarkupParseContext            *ctx;
	GError                         *err = NULL;
	struct lua_xmlrpc_ud            ud;
	gsize                           s;
	gboolean                        res;

	data = luaL_checklstring (L, 1, &s);

	if (data != NULL) {
		ud.L = L;
		ud.parser_state = 0;
		ud.depth = 0;
		ud.param_count = 0;

		ctx = g_markup_parse_context_new (&xmlrpc_parser,
				G_MARKUP_TREAT_CDATA_AS_TEXT | G_MARKUP_PREFIX_ERROR_POSITION, &ud, NULL);
		res = g_markup_parse_context_parse (ctx, data, s, &err);

		if (! res) {
			lua_pushnil (L);
		}
	}
	else {
		lua_pushnil (L);
	}

	return 1;
}

gint
luaopen_xmlrpc (lua_State * L)
{

	luaL_openlib (L, "rspamd_xmlrpc", xmlrpclib_m, 0);

	return 1;
}
