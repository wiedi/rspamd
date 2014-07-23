#ifndef RSPAMD_LUA_H
#define RSPAMD_LUA_H

#include "config.h"
#ifdef WITH_LUA

#include "main.h"
#include "cfg_file.h"
#include "ucl.h"
#include "lua_ucl.h"
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#ifndef lua_open
#define lua_open()  luaL_newstate ()
#endif

#ifndef luaL_reg
#define luaL_reg    luaL_Reg
#endif

#define LUA_ENUM(L, name, val) \
	lua_pushlstring (L, # name, sizeof(# name) - 1); \
	lua_pushnumber (L, val); \
	lua_settable (L, -3);

#if LUA_VERSION_NUM > 501
static inline void
luaL_register (lua_State *L, const gchar *name, const struct luaL_reg *methods)
{
	if (name != NULL) {
		lua_newtable (L);
	}
	luaL_setfuncs (L, methods, 0);
	if (name != NULL) {
		lua_pushvalue (L, -1);
		lua_setglobal (L, name);
	}
}
#endif

/* Interface definitions */
#define LUA_FUNCTION_DEF(class, name) static gint lua_ ## class ## _ ## name ( \
		lua_State * L)
#define LUA_INTERFACE_DEF(class, name) { # name, lua_ ## class ## _ ## name }

extern const luaL_reg null_reg[];

#define RSPAMD_LUA_API_VERSION 12

/* Locked lua state with mutex */
struct lua_locked_state {
	lua_State *L;
	rspamd_mutex_t *m;
};

/* Common utility functions */

/**
 * Create and register new class
 */
void lua_newclass (lua_State *L,
	const gchar *classname,
	const struct luaL_reg *methods);

/**
 * Create and register new class with static methods
 */
void lua_newclass_full (lua_State *L,
	const gchar *classname,
	const gchar *static_name,
	const struct luaL_reg *methods,
	const struct luaL_reg *func);

/**
 * Set class name for object at @param objidx position
 */
void lua_setclass (lua_State *L, const gchar *classname, gint objidx);

/**
 * Set index of table to value (like t['index'] = value)
 */
void lua_set_table_index (lua_State *L, const gchar *index, const gchar *value);

/**
 * Get string value of index in a table (return t['index'])
 */
const gchar * lua_get_table_index_str (lua_State *L, const gchar *index);

/**
 * Convert classname to string
 */
gint lua_class_tostring (lua_State *L);

/**
 * Check whether the argument at specified index is of the specified class
 */
gpointer lua_check_class (lua_State *L, gint index, const gchar *name);

/**
 * Initialize lua and bindings
 */
lua_State * init_lua (struct rspamd_config *cfg);

/**
 * Load and initialize lua plugins
 */
gboolean init_lua_filters (struct rspamd_config *cfg);

/**
 * Initialize new locked lua_State structure
 */
struct lua_locked_state * init_lua_locked (struct rspamd_config *cfg);
/**
 * Free locked state structure
 */
void free_lua_locked (struct lua_locked_state *st);

/**
 * Push lua ip address
 */
void lua_ip_push (lua_State *L, rspamd_inet_addr_t *addr);

/**
 * Push ip address from a string (nil is pushed if a string cannot be converted)
 */
void lua_ip_push_fromstring (lua_State *L, const gchar *ip_str);

/**
 * Create type error
 */
int rspamd_lua_typerror (lua_State *L, int narg, const char *tname);

/**
 * Lua IP address structure
 */
struct rspamd_lua_ip {
	rspamd_inet_addr_t addr;
	gboolean is_valid;
};

/**
 * Open libraries functions
 */
gint luaopen_message (lua_State *L);
gint luaopen_task (lua_State *L);
gint luaopen_config (lua_State *L);
gint luaopen_metric (lua_State *L);
gint luaopen_radix (lua_State *L);
gint luaopen_hash_table (lua_State *L);
gint luaopen_trie (lua_State * L);
gint luaopen_textpart (lua_State *L);
gint luaopen_mimepart (lua_State *L);
gint luaopen_image (lua_State *L);
gint luaopen_url (lua_State *L);
gint luaopen_classifier (lua_State *L);
gint luaopen_statfile (lua_State * L);
gint luaopen_glib_regexp (lua_State *L);
gint luaopen_cdb (lua_State *L);
gint luaopen_xmlrpc (lua_State * L);
gint luaopen_http (lua_State * L);
gint luaopen_redis (lua_State * L);
gint luaopen_upstream (lua_State * L);
gint luaopen_mempool (lua_State * L);
gint luaopen_session (lua_State * L);
gint luaopen_io_dispatcher (lua_State * L);
gint luaopen_dns_resolver (lua_State * L);
gint luaopen_rsa (lua_State * L);
gint luaopen_ip (lua_State * L);

gint lua_call_filter (const gchar *function, struct rspamd_task *task);
gint lua_call_chain_filter (const gchar *function,
	struct rspamd_task *task,
	gint *marks,
	guint number);
double lua_consolidation_func (struct rspamd_task *task,
	const gchar *metric_name,
	const gchar *function_name);
gboolean lua_call_expression_func (gpointer lua_data,
	struct rspamd_task *task,
	GList *args,
	gboolean *res);
void lua_call_post_filters (struct rspamd_task *task);
void lua_call_pre_filters (struct rspamd_task *task);
void add_luabuf (const gchar *line);

/* Classify functions */
GList * call_classifier_pre_callbacks (struct rspamd_classifier_config *ccf,
	struct rspamd_task *task,
	gboolean is_learn,
	gboolean is_spam,
	lua_State *L);
double call_classifier_post_callbacks (struct rspamd_classifier_config *ccf,
	struct rspamd_task *task,
	double in,
	lua_State *L);

double lua_normalizer_func (struct rspamd_config *cfg,
	long double score,
	void *params);

/* Config file functions */
void lua_post_load_config (struct rspamd_config *cfg);
void lua_process_element (struct rspamd_config *cfg,
	const gchar *name,
	const gchar *module_name,
	struct rspamd_module_opt *opt,
	gint idx,
	gboolean allow_meta);
gboolean lua_handle_param (struct rspamd_task *task,
	gchar *mname,
	gchar *optname,
	enum lua_var_type expected_type,
	gpointer *res);
gboolean lua_check_condition (struct rspamd_config *cfg,
	const gchar *condition);
void lua_dumpstack (lua_State *L);

struct memory_pool_s * lua_check_mempool (lua_State * L);


#endif /* WITH_LUA */
#endif /* RSPAMD_LUA_H */
