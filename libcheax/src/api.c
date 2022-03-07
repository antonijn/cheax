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

/* declare associative array of builtin error codes and their names */
CHEAX_BUILTIN_ERROR_NAMES(bltn_error_names);

static int
full_sym_cmp(struct rb_tree *tree, struct rb_node *a, struct rb_node *b)
{
	struct full_sym *fs_a = a->value, *fs_b = b->value;
	return strcmp(fs_a->name, fs_b->name);
}

/*
 * Returns environment e, with !e->is_bif or e == NULL.
 */
static struct chx_env *
norm_env(struct chx_env *env)
{
	while (env != NULL && env->is_bif)
		env = env->value.bif[0];

	return env;
}

struct chx_sym *
find_sym_in(struct chx_env *env, const char *name)
{
	struct full_sym dummy;
	dummy.name = name;
	env = norm_env(env);

	if (env == NULL)
		return NULL;

	struct full_sym *fs = rb_tree_find(&env->value.norm.syms, &dummy);
	return (fs == NULL) ? NULL : &fs->sym;
}

struct chx_sym *
find_sym_in_or_below(struct chx_env *env, const char *name)
{
	if (env == NULL)
		return NULL;

	if (!env->is_bif) {
		struct chx_sym *sym = find_sym_in(env, name);
		if (sym != NULL)
			return sym;

		return find_sym_in_or_below(env->value.norm.below, name);
	}

	for (int i = 0; i < 2; ++i) {
		struct chx_sym *sym = find_sym_in_or_below(env->value.bif[i], name);
		if (sym != NULL)
			return sym;
	}

	return NULL;
}

struct chx_sym *
find_sym(CHEAX *c, const char *name)
{
	struct chx_sym *sym = find_sym_in_or_below(c->env, name);
	return (sym != NULL) ? sym : find_sym_in(&c->globals, name);
}


static struct chx_env *
env_init(CHEAX *c, struct chx_env *env, struct chx_env *below)
{
	rb_tree_init(&env->value.norm.syms, full_sym_cmp);
	env->is_bif = false;
	env->value.norm.syms.info = c;
	env->value.norm.below = below;
	return env;
}

static void
full_sym_node_dealloc(struct rb_tree *syms, struct rb_node *node)
{
	struct full_sym *fs = node->value;
	struct chx_sym *sym = &fs->sym;
	if (sym->fin != NULL)
		sym->fin(syms->info, sym);
	free(fs);
	rb_node_dealloc(node);
}

static void
env_cleanup(void *env_bytes, void *info)
{
	struct chx_env *env = env_bytes;
	rb_tree_cleanup(&env->value.norm.syms, full_sym_node_dealloc);
}

struct chx_env *
cheax_push_env(CHEAX *c)
{
	struct chx_env *env = cheax_alloc_with_fin(c, sizeof(struct chx_env), CHEAX_ENV,
	                                           env_cleanup, NULL);
	return c->env = env_init(c, env, c->env);
}

struct chx_env *
cheax_enter_env(CHEAX *c, struct chx_env *main)
{
	struct chx_env *env = cheax_alloc(c, sizeof(struct chx_env), CHEAX_ENV);
	env->is_bif = true;
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

	if (res->is_bif)
		c->env = res->value.bif[1];
	else
		c->env = res->value.norm.below;

	return res;
}

void
cheax_defmacro(CHEAX *c, const char *id, chx_func_ptr perform, void *info)
{
	cheax_var(c, id, &cheax_ext_func(c, id, perform, info)->base, CHEAX_READONLY);
}

struct chx_sym *
cheax_defsym(CHEAX *c, const char *id,
             chx_getter get, chx_setter set,
             chx_finalizer fin, void *user_info)
{
	if (id == NULL) {
		cry(c, "cheax_defsym", CHEAX_EAPI, "`id' cannot be NULL");
		return NULL;
	}

	if (get == NULL && set == NULL) {
		cry(c, "cheax_defsym", CHEAX_EAPI, "`get' and `set' cannot both be NULL");
		return NULL;
	}

	struct chx_env *env = norm_env(c->env);
	if (env == NULL)
		env = &c->globals;

	if (find_sym_in(env, id) != NULL) {
		cry(c, "defsym", CHEAX_EEXIST, "symbol `%s' already exists", id);
		return NULL;
	}

	size_t idlen = strlen(id);

	char *fs_mem = malloc(sizeof(struct full_sym) + idlen + 1);
	char *idcpy = fs_mem + sizeof(struct full_sym);
	memcpy(idcpy, id, idlen + 1);

	struct full_sym *fs = (struct full_sym *)fs_mem;
	fs->name = idcpy;
	fs->sym.get = get;
	fs->sym.set = set;
	fs->sym.fin = fin;
	fs->sym.user_info = user_info;
	fs->sym.protect = NULL;
	rb_tree_insert(&env->value.norm.syms, fs);
	return &fs->sym;
}

static struct chx_value *
var_get(CHEAX *c, struct chx_sym *sym)
{
	return sym->protect;
}
static void
var_set(CHEAX *c, struct chx_sym *sym, struct chx_value *value)
{
	sym->protect = value;
}

void
cheax_var(CHEAX *c, const char *id, struct chx_value *value, int flags)
{
	struct chx_sym *sym;
	sym = cheax_defsym(c, id,
	                   has_flag(flags, CHEAX_WRITEONLY) ? NULL : var_get,
	                   has_flag(flags, CHEAX_READONLY)  ? NULL : var_set,
	                   NULL, NULL);
	if (sym != NULL)
		sym->protect = value;
}

void
cheax_set(CHEAX *c, const char *id, struct chx_value *value)
{
	if (id == NULL) {
		cry(c, "set", CHEAX_EAPI, "`id' cannot be NULL");
		return;
	}

	struct chx_sym *sym = find_sym(c, id);
	if (sym == NULL) {
		cry(c, "set", CHEAX_ENOSYM, "no such symbol `%s'", id);
		return;
	}

	if (sym->set == NULL)
		cry(c, "set", CHEAX_EREADONLY, "cannot write to read-only symbol");
	else
		sym->set(c, sym, value);
}

struct chx_value *
cheax_get(CHEAX *c, const char *id)
{
	if (id == NULL) {
		cry(c, "get", CHEAX_EAPI, "`id' cannot be NULL");
		return NULL;
	}

	struct chx_sym *sym = find_sym(c, id);
	if (sym == NULL) {
		cry(c, "get", CHEAX_ENOSYM, "no such symbol `%s'", id);
		return NULL;
	}

	if (sym->get == NULL) {
		cry(c, "set", CHEAX_EWRITEONLY, "cannot read from write-only symbol");
		return NULL;
	}

	return sym->get(c, sym);
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
cheax_ext_func(CHEAX *c, const char *name, chx_func_ptr perform, void *info)
{
	if (perform == NULL || name == NULL)
		return NULL;

	struct chx_ext_func *res = cheax_alloc(c, sizeof(struct chx_ext_func), CHEAX_EXT_FUNC);
	res->name = name;
	res->perform = perform;
	res->info = info;
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
		if (len != 0) {
			cry(c, "cheax_nstring", CHEAX_EAPI, "`value' cannot be NULL");
			return NULL;
		}

		value = "";
	}

	struct chx_string *res = cheax_alloc(c, sizeof(struct chx_string) + len + 1, CHEAX_STRING);
	char *buf = ((char *)res) + sizeof(struct chx_string);
	memcpy(buf, value, len);
	buf[len] = '\0';

	res->value = buf;
	res->len = len;
	res->orig = res;

	return res;
}
struct chx_string *
cheax_substr(CHEAX *c, struct chx_string *str, size_t pos, size_t len)
{
	if (str == NULL) {
		cry(c, "substr", CHEAX_EAPI, "`str' cannot be NULL");
		return NULL;
	}

	if (pos > SIZE_MAX - len || pos + len > str->len) {
		cry(c, "substr", CHEAX_EVALUE, "substring out of bounds");
		return NULL;
	}

	struct chx_string *res = cheax_alloc(c, sizeof(struct chx_string), CHEAX_STRING);
	res->value = str->value + pos;
	res->len = len;
	res->orig = str->orig;
	return res;
}

static const char *
errname(CHEAX *c, int code)
{
	if (code >= CHEAX_EUSER0) {
		size_t idx = code - CHEAX_EUSER0;
		if (idx >= c->user_error_names.len) {
			cry(c, "errname", CHEAX_EAPI, "invalid user error code");
			return NULL;
		}
		return c->user_error_names.array[idx];
	}

	/* builtin error code, binary search */

	int lo = 0, hi = sizeof(bltn_error_names) / sizeof(bltn_error_names[0]);
	while (lo <= hi) {
		int pivot = (lo + hi) / 2;
		int pivot_code = bltn_error_names[pivot].code;
		if (pivot_code == code)
			return bltn_error_names[pivot].name;
		if (pivot_code < code)
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

	cheax_var(c, name, set_type(&cheax_int(c, code)->base, CHEAX_ERRORCODE), CHEAX_EREADONLY);
	return code;
}
static void
declare_builtin_errors(CHEAX *c)
{
	int num_codes = sizeof(bltn_error_names)
	              / sizeof(bltn_error_names[0]);

	for (int i = 0; i < num_codes; ++i) {
		const char *name = bltn_error_names[i].name;
		int code = bltn_error_names[i].code;

		cheax_var(c, name,
		          set_type(&cheax_int(c, code)->base, CHEAX_ERRORCODE),
		          CHEAX_READONLY);
	}
}

static bool
match_colon(CHEAX *c, struct chx_list *pan, struct chx_list *match, int flags)
{
	if (pan->next == NULL)
		return cheax_match(c, pan->value, &match->base, flags);

	return match != NULL
	    && cheax_match(c, pan->value, match->value, flags)
	    && match_colon(c, pan->next, match->next, flags);
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

	if (pan_ty == CHEAX_ID) {
		cheax_var(c, ((struct chx_id *)pan)->id, match, flags);
		return true;
	}

	if (pan_ty != cheax_type_of(match))
		return false;

	switch (pan_ty) {
	case CHEAX_LIST:
		return match_list(c, (struct chx_list *)pan, (struct chx_list *)match, flags);
	case CHEAX_NIL:
	case CHEAX_INT:
	case CHEAX_DOUBLE:
	case CHEAX_BOOL:
	case CHEAX_STRING:
		return cheax_eq(c, pan, match);
	default:
		return false;
	}
}

bool
cheax_eq(CHEAX *c, struct chx_value *l, struct chx_value *r)
{
	if (cheax_type_of(l) != cheax_type_of(r))
		return false;

	switch (cheax_resolve_type(c, cheax_type_of(l))) {
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
		;
		struct chx_ext_func *extfl = (struct chx_ext_func *)l;
		struct chx_ext_func *extfr = (struct chx_ext_func *)r;
		return extfl->perform == extfr->perform && extfl->info == extfr->info;
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
	env_init(res, &res->globals, NULL);
	res->env = NULL;

	res->features = 0;

	res->max_stack_depth = 0x1000;
	res->stack_depth = 0;
	res->error.state = CHEAX_RUNNING;
	res->error.code = 0;
	res->error.msg = NULL;

	res->typestore.array = NULL;
	res->typestore.len = res->typestore.cap = 0;

	res->user_error_names.array = NULL;
	res->user_error_names.len = res->user_error_names.cap = 0;

	gcol_init(res);

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
	for (size_t i = 0; i < c->typestore.len; ++i) {
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

	env_cleanup(&c->globals, NULL);
	gcol_destroy(c);

	free(c);
}
const char *
cheax_version(void)
{
	return VERSION_STRING;
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

static bool
can_cast(CHEAX *c, struct chx_value *v, int type)
{
	if (!cheax_is_valid_type(c, type))
		return false;

	int vtype = cheax_type_of(v);
	return vtype == type
	    || cheax_get_base_type(c, vtype) == type
	    || (vtype == CHEAX_INT && type == CHEAX_DOUBLE);
}

struct chx_value *
cheax_cast(CHEAX *c, struct chx_value *v, int type)
{
	if (!can_cast(c, v, type)) {
		cry(c, "cast", CHEAX_ETYPE, "invalid cast");
		return NULL;
	}

	if (cheax_type_of(v) == CHEAX_INT && type == CHEAX_DOUBLE)
		return &cheax_double(c, (double)((struct chx_int *)v)->value)->base;

	struct chx_value *res = cheax_shallow_copy(c, v);
	return (res != NULL) ? set_type(res, type) : res;
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

	int tycode = ts_idx + CHEAX_TYPESTORE_BIAS;
	cheax_var(c, name, set_type(&cheax_int(c, tycode)->base, CHEAX_TYPECODE), CHEAX_READONLY);
	return tycode;
}
int
cheax_find_type(CHEAX *c, const char *name)
{
	if (name == NULL) {
		cry(c, "cheax_find_type", CHEAX_EAPI, "`name' cannot be NULL");
		return -1;
	}

	for (size_t i = 0; i < c->typestore.len; ++i)
		if (!strcmp(name, c->typestore.array[i].name))
			return i + CHEAX_TYPESTORE_BIAS;

	return -1;
}
bool
cheax_is_valid_type(CHEAX *c, int type)
{
	return cheax_is_basic_type(c, type) || cheax_is_user_type(c, type);
}
bool
cheax_is_basic_type(CHEAX *c, int type)
{
	return type >= 0 && type <= CHEAX_LAST_BASIC_TYPE;
}
bool
cheax_is_user_type(CHEAX *c, int type)
{
	return type >= CHEAX_TYPESTORE_BIAS && (size_t)(type - CHEAX_TYPESTORE_BIAS) < c->typestore.len;
}
int
cheax_get_base_type(CHEAX *c, int type)
{
	if (cheax_is_basic_type(c, type))
		return type;

	if (!cheax_is_user_type(c, type)) {
		cry(c, "cheax_get_base_type", CHEAX_EEVAL, "unable to resolve type");
		return -1;
	}

	return c->typestore.array[type - CHEAX_TYPESTORE_BIAS].base_type;
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


bool
try_vtod(struct chx_value *value, double *res)
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
try_vtoi(struct chx_value *value, int *res)
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


static struct chx_value *
sync_int_get(CHEAX *c, struct chx_sym *sym)
{
	return &cheax_int(c, *(int *)sym->user_info)->base;
}
static void
sync_int_set(CHEAX *c, struct chx_sym *sym, struct chx_value *value)
{
	if (!try_vtoi(value, sym->user_info))
		cry(c, "set", CHEAX_ETYPE, "invalid type");
}

void
cheax_sync_int(CHEAX *c, const char *name, int *var, int flags)
{
	cheax_defsym(c, name,
	             has_flag(flags, CHEAX_WRITEONLY) ? NULL : sync_int_get,
	             has_flag(flags, CHEAX_READONLY)  ? NULL : sync_int_set,
	             NULL, var);
}

static struct chx_value *
sync_float_get(CHEAX *c, struct chx_sym *sym)
{
	return &cheax_double(c, *(float *)sym->user_info)->base;
}
static void
sync_float_set(CHEAX *c, struct chx_sym *sym, struct chx_value *value)
{
	double d;
	if (!try_vtod(value, &d))
		cry(c, "set", CHEAX_ETYPE, "invalid type");
	else
		*(float *)sym->user_info = d;
}

void
cheax_sync_float(CHEAX *c, const char *name, float *var, int flags)
{
	cheax_defsym(c, name,
	             has_flag(flags, CHEAX_WRITEONLY) ? NULL : sync_float_get,
	             has_flag(flags, CHEAX_READONLY)  ? NULL : sync_float_set,
	             NULL, var);
}

static struct chx_value *
sync_double_get(CHEAX *c, struct chx_sym *sym)
{
	return &cheax_double(c, *(double *)sym->user_info)->base;
}
static void
sync_double_set(CHEAX *c, struct chx_sym *sym, struct chx_value *value)
{
	if (!try_vtod(value, sym->user_info))
		cry(c, "set", CHEAX_ETYPE, "invalid type");
}

void
cheax_sync_double(CHEAX *c, const char *name, double *var, int flags)
{
	cheax_defsym(c, name,
	             has_flag(flags, CHEAX_WRITEONLY) ? NULL : sync_double_get,
	             has_flag(flags, CHEAX_READONLY)  ? NULL : sync_double_set,
	             NULL, var);
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

