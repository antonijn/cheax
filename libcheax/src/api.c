/* Copyright (c) 2022, Antonie Blom
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <cheax.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>

#include "api.h"
#include "config.h"
#include "gc.h"

static int
var_cmp(struct rb_tree *tree, struct rb_node *a, struct rb_node *b)
{
	struct variable *var_a = a->value, *var_b = b->value;
	return strcmp(var_a->name, var_b->name);
}

/*
 * Returns environment e, with has_bif_env_bit(e) == false or e == NULL.
 */
static struct chx_env *
norm_env(struct chx_env *env)
{
	if (env == NULL)
		return NULL;

	return has_bif_env_bit(&env->base) ? norm_env(env->value.bif[0]) : env;
}

struct variable *
find_sym_in(struct chx_env *env, const char *name)
{
	struct variable dummy;
	dummy.name = name;
	env = norm_env(env);

	return (env == NULL) ? NULL : rb_tree_find(&env->value.norm.syms, &dummy);
}

struct variable *
find_sym_in_or_below(struct chx_env *env, const char *name)
{
	if (env == NULL)
		return NULL;

	if (!has_bif_env_bit(&env->base)) {
		struct variable *var = find_sym_in(env, name);
		if (var != NULL)
			return var;

		return find_sym_in_or_below(env->value.norm.below, name);
	}

	for (int i = 0; i < 2; ++i) {
		struct variable *var = find_sym_in_or_below(env->value.bif[i], name);
		if (var != NULL)
			return var;
	}

	return NULL;
}

struct variable *
find_sym(CHEAX *c, const char *name)
{
	struct variable *var = find_sym_in_or_below(c->env, name);
	return (var != NULL) ? var : find_sym_in(&c->globals, name);
}

static struct variable *
def_sym_in(CHEAX *c, struct chx_env *env, const char *name, int flags)
{
	env = norm_env(env);
	if (env == NULL)
		env = &c->globals;

	if (find_sym_in(env, name) != NULL) {
		cry(c, "def", CHEAX_EEXIST, "symbol `%s' already exists", name);
		return NULL;
	}

	struct variable *new = malloc(sizeof(struct variable));
	new->flags = flags;
	new->ctype = CTYPE_NONE;
	new->value.norm = NULL;
	new->name = name;
	rb_tree_insert(&env->value.norm.syms, new);
	return new;
}

static struct variable *
def_sym(CHEAX *c, const char *name, int flags)
{
	return def_sym_in(c, c->env, name, flags);
}

static struct chx_env *
env_init(struct chx_env *env, struct chx_env *below)
{
	rb_tree_init(&env->value.norm.syms, var_cmp);
	env->value.norm.below = below;
	return env;
}

static void
var_node_dealloc(struct rb_tree *syms, struct rb_node *node)
{
	free(node->value);
	rb_node_dealloc(node);
}

static void
env_cleanup(void *env_bytes, void *info)
{
	struct chx_env *env = env_bytes;
	rb_tree_cleanup(&env->value.norm.syms, var_node_dealloc);
}

struct chx_env *
cheax_push_env(CHEAX *c)
{
	struct chx_env *env = cheax_alloc_with_fin(c, sizeof(struct chx_env), CHEAX_ENV,
	                                           env_cleanup, NULL);
	return c->env = env_init(env, c->env);
}

struct chx_env *
cheax_enter_env(CHEAX *c, struct chx_env *main)
{
	struct chx_env *env = cheax_alloc(c, sizeof(struct chx_env), CHEAX_ENV);
	env->base.type |= BIF_ENV_BIT;
	env->value.bif[0] = main;
	env->value.bif[1] = c->env;
	return c->env = env;
}

struct chx_env *
cheax_pop_env(CHEAX *c)
{
	struct chx_env *res = c->env;
	if (res == NULL) {
		cry(c, "cheax_pop_env", CHEAX_EAPI, "cannot pop NULL env");
		return NULL;
	}

	if (has_bif_env_bit(&res->base))
		c->env = res->value.bif[1];
	else
		c->env = res->value.norm.below;

	return res;
}

void
cheax_defmacro(CHEAX *c, const char *id, chx_func_ptr perform)
{
	cheax_var(c, id, &cheax_ext_func(c, perform, id)->base, CHEAX_READONLY);
}

void
cheax_var(CHEAX *c, const char *id, struct chx_value *value, int flags)
{
	if (id == NULL) {
		cry(c, "var", CHEAX_EAPI, "`id' cannot be NULL");
		return;
	}

	struct variable *sym = def_sym(c, id, flags & ~CHEAX_SYNCED);
	if (sym == NULL)
		return;
	sym->value.norm = value;
}


bool
try_convert_to_double(struct chx_value *value, double *res)
{
	switch (cheax_type_of(value)) {
	case CHEAX_INT:
		*res = ((struct chx_int *)value)->value;
		return true;
	case CHEAX_DOUBLE:
		*res = ((struct chx_double *)value)->value;
		return true;
	default:
		return false;
	}
}
bool
try_convert_to_int(struct chx_value *value, int *res)
{
	switch (cheax_type_of(value)) {
	case CHEAX_INT:
		*res = ((struct chx_int *)value)->value;
		return true;
	case CHEAX_DOUBLE:
		*res = ((struct chx_double *)value)->value;
		return true;
	default:
		return false;
	}
}


void
cheax_set(CHEAX *c, const char *id, struct chx_value *value)
{
	if (id == NULL) {
		cry(c, "set", CHEAX_EAPI, "`id' cannot be NULL");
		return;
	}

	struct variable *sym = find_sym(c, id);
	if (sym == NULL) {
		cry(c, "set", CHEAX_ENOSYM, "no such symbol \"%s\"", id);
		return;
	}

	if (sym->flags & CHEAX_READONLY) {
		cry(c, "set", CHEAX_EREADONLY, "cannot write to read-only symbol");
		return;
	}

	if ((sym->flags & CHEAX_SYNCED) == 0) {
		sym->value.norm = value;
		return;
	}

	switch (sym->ctype) {
	case CTYPE_INT:
		if (!try_convert_to_int(value, sym->value.sync_int)) {
			cry(c, "set", CHEAX_ETYPE, "invalid type");
			return;
		}
		break;

	case CTYPE_FLOAT:
		; double d;
		if (!try_convert_to_double(value, &d)) {
			cry(c, "set", CHEAX_ETYPE, "invalid type");
			return;
		}
		*sym->value.sync_float = d;
		break;

	case CTYPE_DOUBLE:
		if (!try_convert_to_double(value, sym->value.sync_double)) {
			cry(c, "set", CHEAX_ETYPE, "invalid type");
			return;
		}
		break;

	default:
		cry(c, "set", CHEAX_EEVAL, "unexpected sync-type");
		return;
	}
}

struct chx_value *
cheax_get(CHEAX *c, const char *id)
{
	if (id == NULL) {
		cry(c, "get", CHEAX_EAPI, "`id' cannot be NULL");
		return NULL;
	}

	struct variable *sym = find_sym(c, id);
	if (sym == NULL) {
		cry(c, "get", CHEAX_ENOSYM, "no such symbol `%s'", id);
		return NULL;
	}

	if ((sym->flags & CHEAX_SYNCED) == 0)
		return sym->value.norm;

	switch (sym->ctype) {
	case CTYPE_INT:
		return &cheax_int(c, *sym->value.sync_int)->base;
	case CTYPE_DOUBLE:
		return &cheax_double(c, *sym->value.sync_double)->base;
	case CTYPE_FLOAT:
		return &cheax_double(c, *sym->value.sync_float)->base;
	default:
		cry(c, "get", CHEAX_EEVAL, "unexpected sync-type");
		return NULL;
	}
}


struct chx_quote *
cheax_quote(CHEAX *c, struct chx_value *value)
{
	struct chx_quote *res = cheax_alloc(c, sizeof(struct chx_quote), CHEAX_QUOTE);
	res->value = value;
	return res;
}
struct chx_quote *
cheax_backquote(CHEAX *c, struct chx_value *value)
{
	struct chx_quote *res = cheax_alloc(c, sizeof(struct chx_quote), CHEAX_BACKQUOTE);
	res->value = value;
	return res;
}
struct chx_quote *
cheax_comma(CHEAX *c, struct chx_value *value)
{
	struct chx_quote *res = cheax_alloc(c, sizeof(struct chx_quote), CHEAX_COMMA);
	res->base.type = CHEAX_COMMA;
	res->value = value;
	return res;
}
struct chx_int *
cheax_int(CHEAX *c, int value)
{
	struct chx_int *res = cheax_alloc(c, sizeof(struct chx_int), CHEAX_INT);
	res->value = value;
	return res;
}
struct chx_int *
cheax_true(CHEAX *c)
{
	static struct chx_int yes = { { CHEAX_BOOL | NO_GC_BIT }, 1 };
	return &yes;
}
struct chx_int *
cheax_false(CHEAX *c)
{
	static struct chx_int no = { { CHEAX_BOOL | NO_GC_BIT }, 0 };
	return &no;
}
struct chx_int *
cheax_bool(CHEAX *c, bool value)
{
	return value ? cheax_true(c) : cheax_false(c);
}
struct chx_double *
cheax_double(CHEAX *c, double value)
{
	struct chx_double *res = cheax_alloc(c, sizeof(struct chx_double), CHEAX_DOUBLE);
	res->value = value;
	return res;
}
struct chx_user_ptr *
cheax_user_ptr(CHEAX *c, void *value, int type)
{
	if (cheax_is_basic_type(c, type) || cheax_resolve_type(c, type) != CHEAX_USER_PTR) {
		cry(c, "cheax_user_ptr", CHEAX_EAPI, "invalid user pointer type");
		return NULL;
	}
	struct chx_user_ptr *res = cheax_alloc(c, sizeof(struct chx_user_ptr), type);
	res->value = value;
	return res;
}
struct chx_id *
cheax_id(CHEAX *c, const char *id)
{
	if (id == NULL)
		return NULL;

	/* NOTE: the GC depends on chx_id having this memory layout */
	struct chx_id *res = cheax_alloc(c, sizeof(struct chx_id) + strlen(id) + 1, CHEAX_ID);
	char *buf = ((char *)res) + sizeof(struct chx_id);
	strcpy(buf, id);

	res->id = buf;

	return res;
}
struct chx_list *
cheax_list(CHEAX *c, struct chx_value *car, struct chx_list *cdr)
{
	struct chx_list *res = cheax_alloc(c, sizeof(struct chx_list), CHEAX_LIST);
	res->value = car;
	res->next = cdr;
	return res;
}
struct chx_ext_func *
cheax_ext_func(CHEAX *c, chx_func_ptr perform, const char *name)
{
	if (perform == NULL || name == NULL)
		return NULL;

	struct chx_ext_func *res = cheax_alloc(c, sizeof(struct chx_ext_func), CHEAX_EXT_FUNC);
	res->perform = perform;
	res->name = name;
	return res;
}
struct chx_string *
cheax_string(CHEAX *c, const char *value)
{
	if (value == NULL) {
		cry(c, "cheax_string", CHEAX_EAPI, "`value' cannot be NULL");
		return NULL;
	}

	return cheax_nstring(c, value, strlen(value));
}
struct chx_string *
cheax_nstring(CHEAX *c, const char *value, size_t len)
{
	if (value == NULL) {
		if (len == 0) {
			value = "";
		} else {
			cry(c, "cheax_nstring", CHEAX_EAPI, "`value' cannot be NULL");
			return NULL;
		}
	}

	struct chx_string *res = cheax_alloc(c, sizeof(struct chx_string) + len + 1, CHEAX_STRING);
	char *buf = ((char *)res) + sizeof(struct chx_string);
	memcpy(buf, value, len);
	buf[len] = '\0';

	res->value = buf;
	res->len = len;

	return res;
}

static const char *
errname(CHEAX *c, int code)
{
	if (code >= CHEAX_EUSER0) {
		int idx = code - CHEAX_EUSER0;
		if (idx >= c->user_error_names.len) {
			cry(c, "errname", CHEAX_EAPI, "invalid user error code");
			return NULL;
		}
		return c->user_error_names.array[idx];
	}

	/* builtin error code, binary search */

	int lo = 0;
	int hi = sizeof(cheax_builtin_error_codes)
	       / sizeof(cheax_builtin_error_codes[0]);

	while (lo <= hi) {
		int pivot = (lo + hi) / 2;
		const char *pivot_name = cheax_builtin_error_codes[pivot].name;
		int pivot_code = cheax_builtin_error_codes[pivot].code;
		if (pivot_code == code)
			return pivot_name;
		else if (pivot_code < code)
			lo = pivot + 1;
		else
			hi = pivot - 1;
	}

	cry(c, "errname", CHEAX_EAPI, "invalid error code");
	return NULL;
}
int
cheax_errstate(CHEAX *c)
{
	return c->error.state;
}
int
cheax_errno(CHEAX *c)
{
	return c->error.code;
}
void
cheax_perror(CHEAX *c, const char *s)
{
	int err = cheax_errno(c);
	if (err == 0)
		return;

	if (s != NULL)
		fprintf(stderr, "%s: ", s);

	if (c->error.msg != NULL)
		fprintf(stderr, "%s ", c->error.msg->value);

	const char *ename = errname(c, err);
	if (ename != NULL)
		fprintf(stderr, "[%s]", ename);
	else
		fprintf(stderr, "[code %x]", err);

	fprintf(stderr, "\n");
}
void
cheax_clear_errno(CHEAX *c)
{
	c->error.state = CHEAX_RUNNING;
	c->error.code = 0;
	c->error.msg = NULL;
}
void
cheax_throw(CHEAX *c, int code, struct chx_string *msg)
{
	if (code == 0) {
		cry(c, "throw", CHEAX_EAPI, "cannot throw error code 0");
		return;
	}

	c->error.state = CHEAX_THROWN;
	c->error.code = code;
	c->error.msg = msg;
}
int
cheax_new_error_code(CHEAX *c, const char *name)
{
	if (name == NULL) {
		cry(c, "new_error_code", CHEAX_EAPI, "`name' cannot be NULL");
		return -1;
	}

	int code = CHEAX_EUSER0 + c->user_error_names.len++;
	if (c->user_error_names.len > c->user_error_names.cap) {
		c->user_error_names.cap = ((c->user_error_names.cap / 2) + 1) * 3;
		c->user_error_names.array = realloc(c->user_error_names.array, c->user_error_names.cap * sizeof(const char *));
	}

	c->user_error_names.array[code - CHEAX_EUSER0] = name;

	struct chx_value *errcode = &cheax_int(c, code)->base;
	set_type(errcode, CHEAX_ERRORCODE);
	cheax_var(c, name, errcode, CHEAX_EREADONLY);

	return code;
}
static void
declare_builtin_errors(CHEAX *c)
{
	int num_codes = sizeof(cheax_builtin_error_codes)
	              / sizeof(cheax_builtin_error_codes[0]);

	for (int i = 0; i < num_codes; ++i) {
		const char *name = cheax_builtin_error_codes[i].name;
		int code = cheax_builtin_error_codes[i].code;

		struct chx_value *errcode = &cheax_int(c, code)->base;
		set_type(errcode, CHEAX_ERRORCODE);
		cheax_var(c, name, errcode, CHEAX_READONLY);
	}
}

static bool
match_colon(CHEAX *c,
            struct chx_list *pan,
            struct chx_list *match,
            int flags)
{
	if (pan->next == NULL)
		return cheax_match(c, pan->value, &match->base, flags);
	if (match == NULL)
		return false;
	if (!cheax_match(c, pan->value, match->value, flags))
		return false;
	return match_colon(c, pan->next, match->next, flags);
}

static bool
match_list(CHEAX *c, struct chx_list *pan, struct chx_list *match, int flags)
{
	if (cheax_type_of(pan->value) == CHEAX_ID
	 && strcmp((((struct chx_id *)pan->value)->id), ":") == 0)
	{
		return match_colon(c, pan->next, match, flags);
	}

	while (pan != NULL && match != NULL) {
		if (!cheax_match(c, pan->value, match->value, flags))
			return false;

		pan = pan->next;
		match = match->next;
	}

	return (pan == NULL) && (match == NULL);
}

bool
cheax_match(CHEAX *c, struct chx_value *pan, struct chx_value *match, int flags)
{
	int pan_ty = cheax_type_of(pan);

	if (pan_ty == CHEAX_NIL)
		return match == NULL;

	if (pan_ty == CHEAX_ID) {
		/* don't worry that "pan" will be wrongfully flagged as
		 * garbage, the GC detects that this string is from a
		 * chx_id. */
		cheax_var(c, ((struct chx_id *)pan)->id, match, flags);
		return true;
	}

	if (match == NULL)
		return false;

	struct chx_list *pan_list;

	switch (pan_ty) {
	case CHEAX_INT:
	case CHEAX_DOUBLE:
	case CHEAX_BOOL:
	case CHEAX_STRING:
		return cheax_eq(c, pan, match);

	case CHEAX_LIST:
		pan_list = (struct chx_list *)pan;
		if (cheax_type_of(match) != CHEAX_LIST)
			return false;
		struct chx_list *mlist = (struct chx_list *)match;

		return match_list(c, pan_list, mlist, flags);

	default:
		return false;
	}
}

bool
cheax_eq(CHEAX *c, struct chx_value *l, struct chx_value *r)
{
	if (cheax_type_of(l) != cheax_type_of(r))
		return false;

	int ty = cheax_resolve_type(c, cheax_type_of(l));

	switch (ty) {
	case CHEAX_NIL:
		return true;
	case CHEAX_ID:
		return strcmp(((struct chx_id *)l)->id, ((struct chx_id *)r)->id) == 0;
	case CHEAX_INT:
	case CHEAX_BOOL:
		return ((struct chx_int *)l)->value == ((struct chx_int *)r)->value;
	case CHEAX_DOUBLE:
		return ((struct chx_double *)l)->value == ((struct chx_double *)r)->value;
	case CHEAX_LIST:
		;
		struct chx_list *llist = (struct chx_list *)l;
		struct chx_list *rlist = (struct chx_list *)r;
		return cheax_eq(c, llist->value, rlist->value)
		    && cheax_eq(c, &llist->next->base, &rlist->next->base);
	case CHEAX_EXT_FUNC:
		return ((struct chx_ext_func *)l)->perform == ((struct chx_ext_func *)r)->perform;
	case CHEAX_QUOTE:
	case CHEAX_BACKQUOTE:
	case CHEAX_COMMA:
		return cheax_eq(c, ((struct chx_quote *)l)->value, ((struct chx_quote *)r)->value);
	case CHEAX_STRING:
		;
		struct chx_string *lstring = (struct chx_string *)l;
		struct chx_string *rstring = (struct chx_string *)r;
		return (lstring->len == rstring->len)
		    && memcmp(lstring->value, rstring->value, lstring->len) == 0;
	case CHEAX_USER_PTR:
		return ((struct chx_user_ptr *)l)->value == ((struct chx_user_ptr *)r)->value;
	default:
		return l == r;
	}
}

void
cry(CHEAX *c, const char *name, int err, const char *frmt, ...)
{
	va_list ap;

	size_t preamble_len = strlen(name) + 4;

	va_start(ap, frmt);
	size_t msglen = preamble_len + vsnprintf(NULL, 0, frmt, ap);
	va_end(ap);

	char *buf = malloc(msglen + 1);
	sprintf(buf, "(%s): ", name);
	va_start(ap, frmt);
	vsnprintf(buf + preamble_len, msglen - preamble_len + 1, frmt, ap);
	va_end(ap);

	cheax_throw(c, err, cheax_nstring(c, buf, msglen));

	free(buf);
}

CHEAX *
cheax_init(void)
{
	CHEAX *res = malloc(sizeof(struct cheax));
	res->globals.base.type = CHEAX_ENV | NO_GC_BIT;
	env_init(&res->globals, NULL);
	res->env = NULL;

	res->max_stack_depth = 0x1000;
	res->stack_depth = 0;
	res->error.state = CHEAX_RUNNING;
	res->error.code = 0;
	res->error.msg = NULL;

	res->typestore.array = NULL;
	res->typestore.len = res->typestore.cap = 0;

	res->user_error_names.array = NULL;
	res->user_error_names.len = res->user_error_names.cap = 0;

	cheax_gc_init(res);

	/* This is a bit hacky; we declare the these types as aliases
	 * in the typestore, while at the same time we have the
	 * CHEAX_... constants. Bacause CHEAX_TYPECODE is the same
	 * as CHEAX_LAST_BASIC_TYPE + 1, it refers to the first element
	 * in the type store. CHEAX_ERRORCODE the second, etc. As long
	 * as the order is correct, it works. */
	cheax_new_type(res, "TypeCode", CHEAX_INT);
	cheax_new_type(res, "ErrorCode", CHEAX_INT);

	declare_builtin_errors(res);
	export_builtins(res);
	return res;
}
void
cheax_destroy(CHEAX *c)
{
	for (int i = 0; i < c->typestore.len; ++i) {
		struct type_cast *cnext;
		for (struct type_cast *cast = c->typestore.array[i].casts;
		     cast != NULL;
		     cast = cnext)
		{
			cnext = cast->next;
			free(c);
		}
	}
	free(c->typestore.array);

	free(c->user_error_names.array);

	struct rb_node *obj_node;
	while ((obj_node = c->gc.all_objects.root) != NULL)
		cheax_free(c, obj_node->value);

	free(c);
}
const char *
cheax_version(void)
{
	static const char ver[] = VERSION_STRING;
	return ver;
}

int
cheax_list_to_array(CHEAX *c,
                    struct chx_list *list,
                    struct chx_value ***array_ptr,
                    size_t *length)
{
	size_t len = 0, cap = 0;
	struct chx_value **res = NULL;

	if (length == NULL) {
		cry(c, "list_to_array", CHEAX_EAPI, "`length' cannot be NULL");
		return -1;
	}

	if (array_ptr == NULL) {
		cry(c, "list_to_array", CHEAX_EAPI, "`array_ptr' cannot be NULL");
		return -1;
	}

	for (; list != NULL; list = list->next) {
		if (++len > cap) {
			cap = len + len / 2;
			struct chx_value **new_res = realloc(res, sizeof(*res) * cap);
			if (new_res == NULL) {
				cry(c, "list_to_array", CHEAX_ENOMEM, "realloc() failure");
				free(res);
				*length = 0;
				*array_ptr = NULL;
				return -1;
			}
			res = new_res;
		}

		res[len - 1] = list->value;
	}

	*array_ptr = res;
	*length = len;
	return 0;
}


int
cheax_get_max_stack_depth(CHEAX *c)
{
	return c->max_stack_depth;
}
void
cheax_set_max_stack_depth(CHEAX *c, int max_stack_depth)
{
	if (max_stack_depth > 0)
		c->max_stack_depth = max_stack_depth;
	else
		cry(c, "cheax_set_max_stack_depth", CHEAX_EAPI, "maximum stack depth must be positive");
}


struct chx_value *
cheax_shallow_copy(CHEAX *c, struct chx_value *v)
{
	int act_type = cheax_type_of(v);

	size_t size;
	switch (cheax_resolve_type(c, act_type)) {
	case CHEAX_NIL:
		return NULL;
	case CHEAX_ID:
		size = sizeof(struct chx_id);
		break;
	case CHEAX_INT:
	case CHEAX_BOOL:
		size = sizeof(struct chx_int);
		break;
	case CHEAX_DOUBLE:
		size = sizeof(struct chx_double);
		break;
	case CHEAX_LIST:
		size = sizeof(struct chx_list);
		break;
	case CHEAX_FUNC:
	case CHEAX_MACRO:
		size = sizeof(struct chx_func);
		break;
	case CHEAX_EXT_FUNC:
		size = sizeof(struct chx_ext_func);
		break;
	case CHEAX_QUOTE:
	case CHEAX_BACKQUOTE:
	case CHEAX_COMMA:
		size = sizeof(struct chx_quote);
		break;
	case CHEAX_STRING:
		size = sizeof(struct chx_string);
		break;
	case CHEAX_USER_PTR:
		size = sizeof(struct chx_user_ptr);
		break;
	case CHEAX_ENV:
		size = sizeof(struct chx_env);
		break;
	}

	struct chx_value *cpy = cheax_alloc(c, size, act_type);
	int was_type = cpy->type;
	memcpy(cpy, v, size);
	cpy->type = was_type;

	return cpy;
}

struct chx_value *
cheax_cast(CHEAX *c, struct chx_value *v, int type)
{
	/* TODO: improve critria */
	if (cheax_resolve_type(c, cheax_type_of(v)) != cheax_resolve_type(c, type)) {
		cry(c, "cast", CHEAX_ETYPE, "invalid cast");
		return NULL;
	}

	struct chx_value *res = cheax_shallow_copy(c, v);
	if (res != NULL)
		set_type(res, cheax_type_of(v));

	return res;
}

int
cheax_type_of(struct chx_value *v)
{
	return (v == NULL) ? CHEAX_NIL : v->type & CHEAX_TYPE_MASK;
}
int
cheax_new_type(CHEAX *c, const char *name, int base_type)
{
	if (name == NULL) {
		cry(c, "cheax_new_type", CHEAX_EAPI, "`name' cannot be NULL");
		return -1;
	}

	if (!cheax_is_valid_type(c, base_type)) {
		cry(c, "cheax_new_type", CHEAX_EAPI, "`base_type' is not a valid type");
		return -1;
	}

	if (cheax_find_type(c, name) != -1) {
		cry(c, "cheax_new_type", CHEAX_EAPI, "`%s' already exists as a type", name);
		return -1;
	}

	int ts_idx = c->typestore.len++;

	if (c->typestore.len > c->typestore.cap) {
		c->typestore.cap = ((c->typestore.cap / 2) + 1) * 3;
		c->typestore.array = realloc(c->typestore.array, c->typestore.cap * sizeof(struct type_alias));
	}

	struct type_alias alias = { 0 };
	alias.name = name;
	alias.base_type = base_type;
	alias.print = NULL;
	alias.casts = NULL;
	c->typestore.array[ts_idx] = alias;

	int typecode = ts_idx + CHEAX_TYPESTORE_BIAS;

	struct chx_value *value = &cheax_int(c, typecode)->base;
	set_type(value, CHEAX_TYPECODE);
	cheax_var(c, name, value, CHEAX_READONLY);

	return typecode;
}
int
cheax_find_type(CHEAX *c, const char *name)
{
	if (name == NULL) {
		cry(c, "cheax_find_type", CHEAX_EAPI, "`name' cannot be NULL");
		return -1;
	}

	for (int i = 0; i < c->typestore.len; ++i)
		if (!strcmp(name, c->typestore.array[i].name))
			return i + CHEAX_TYPESTORE_BIAS;

	return -1;
}
bool
cheax_is_valid_type(CHEAX *c, int type)
{
	if (type < 0)
		return false;

	return cheax_is_basic_type(c, type) || (type - CHEAX_TYPESTORE_BIAS) < c->typestore.len;
}
bool
cheax_is_basic_type(CHEAX *c, int type)
{
	return type >= 0 && type <= CHEAX_LAST_BASIC_TYPE;
}
int
cheax_get_base_type(CHEAX *c, int type)
{
	if (cheax_is_basic_type(c, type))
		return type;

	int ts_idx = type - CHEAX_TYPESTORE_BIAS;
	if (ts_idx >= c->typestore.len) {
		cry(c, "cheax_get_base_type", CHEAX_EEVAL, "unable to resolve type");
		return -1;
	}

	return c->typestore.array[ts_idx].base_type;
}
int
cheax_resolve_type(CHEAX *c, int type)
{
	while (type > CHEAX_LAST_BASIC_TYPE) {
		int base_type = cheax_get_base_type(c, type);

		if (base_type == -1)
			return -1;

		if (base_type == type) {
			cry(c, "cheax_resolve_type", CHEAX_EEVAL, "unable to resolve type");
			return -1;
		}

		type = base_type;
	}

	return type;
}

void
cheax_sync_int(CHEAX *c, const char *name, int *var, int flags)
{
	struct variable *newsym = def_sym(c, name, flags | CHEAX_SYNCED);
	if (newsym != NULL) {
		newsym->ctype = CTYPE_INT;
		newsym->value.sync_int = var;
	}
}
void
cheax_sync_float(CHEAX *c, const char *name, float *var, int flags)
{
	struct variable *newsym = def_sym(c, name, flags | CHEAX_SYNCED);
	if (newsym != NULL) {
		newsym->ctype = CTYPE_FLOAT;
		newsym->value.sync_float = var;
	}
}
void
cheax_sync_double(CHEAX *c, const char *name, double *var, int flags)
{
	struct variable *newsym = def_sym(c, name, flags | CHEAX_SYNCED);
	if (newsym != NULL) {
		newsym->ctype = CTYPE_DOUBLE;
		newsym->value.sync_double = var;
	}
}

int
cheax_load_prelude(CHEAX *c)
{
	const char *path = CMAKE_INSTALL_PREFIX "/share/cheax/prelude.chx";
	FILE *f = fopen(path, "rb");
	if (f == NULL) {
		cry(c, "cheax_load_prelude", CHEAX_EAPI, "prelude not found at '%s'", path);
		return -1;
	}

	cheax_exec(c, f);
	fclose(f);

	return (cheax_errno(c) == 0) ? 0 : -1;
}

