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

#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "core.h"
#include "err.h"
#include "feat.h"
#include "gc.h"
#include "setup.h"
#include "unpack.h"

struct chx_quote *
cheax_quote(CHEAX *c, struct chx_value *value)
{
	struct chx_quote *res = gcol_alloc(c, sizeof(struct chx_quote), CHEAX_QUOTE);
	if (res != NULL)
		res->value = value;
	return res;
}
struct chx_quote *
cheax_backquote(CHEAX *c, struct chx_value *value)
{
	struct chx_quote *res = gcol_alloc(c, sizeof(struct chx_quote), CHEAX_BACKQUOTE);
	if (res != NULL)
		res->value = value;
	return res;
}
struct chx_quote *
cheax_comma(CHEAX *c, struct chx_value *value)
{
	struct chx_quote *res = gcol_alloc(c, sizeof(struct chx_quote), CHEAX_COMMA);
	if (res != NULL)
		res->value = value;
	return res;
}
struct chx_int *
cheax_int(CHEAX *c, int value)
{
	struct chx_int *res = gcol_alloc(c, sizeof(struct chx_int), CHEAX_INT);
	if (res != NULL)
		res->value = value;
	return res;
}
struct chx_int *
typecode(CHEAX *c, int value)
{
	struct chx_int *res = cheax_int(c, value);
	if (res != NULL)
		res->base.type = CHEAX_TYPECODE;
	return res;
}
struct chx_int *
errorcode(CHEAX *c, int value)
{
	struct chx_int *res = cheax_int(c, value);
	if (res != NULL)
		res->base.type = CHEAX_ERRORCODE;
	return res;
}
struct chx_int *
cheax_true(CHEAX *c)
{
	static struct chx_int yes = { { CHEAX_BOOL, 0 }, 1 };
	return &yes;
}
struct chx_int *
cheax_false(CHEAX *c)
{
	static struct chx_int no = { { CHEAX_BOOL, 0 }, 0 };
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
	struct chx_double *res = gcol_alloc(c, sizeof(struct chx_double), CHEAX_DOUBLE);
	if (res != NULL)
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
	struct chx_user_ptr *res = gcol_alloc(c, sizeof(struct chx_user_ptr), type);
	if (res != NULL)
		res->value = value;
	return res;
}
struct chx_id *
cheax_id(CHEAX *c, const char *id)
{
	if (id == NULL)
		return NULL;

	struct chx_id *res = gcol_alloc(c, sizeof(struct chx_id) + strlen(id) + 1, CHEAX_ID);
	if (res != NULL) {
		char *buf = ((char *)res) + sizeof(struct chx_id);
		strcpy(buf, id);

		res->id = buf;
	}
	return res;
}
struct chx_list *
cheax_list(CHEAX *c, struct chx_value *car, struct chx_list *cdr)
{
	struct chx_list *res = gcol_alloc(c, sizeof(struct chx_list), CHEAX_LIST);
	if (res != NULL) {
		res->value = car;
		res->next = cdr;
	}
	return res;
}
struct debug_list*
debug_list(CHEAX *c, struct chx_value *car, struct chx_list *cdr, struct debug_info info)
{
	struct debug_list *res;
	res = gcol_alloc(c, sizeof(struct debug_list), CHEAX_LIST);
	if (res != NULL) {
		res->base.base.rtflags |= DEBUG_LIST;
		res->base.value = car;
		res->base.next = cdr;
		res->info = info;
	}
	return res;
}
struct chx_ext_func *
cheax_ext_func(CHEAX *c, const char *name, chx_func_ptr perform, void *info)
{
	if (perform == NULL || name == NULL)
		return NULL;

	struct chx_ext_func *res = gcol_alloc(c, sizeof(struct chx_ext_func), CHEAX_EXT_FUNC);
	if (res != NULL) {
		res->name = name;
		res->perform = perform;
		res->info = info;
	}
	return res;
}
size_t
cheax_strsize(CHEAX *c, struct chx_string *str)
{
	return (str == NULL) ? 0 : str->len;
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

	struct chx_string *res = gcol_alloc(c, sizeof(struct chx_string) + len + 1, CHEAX_STRING);
	if (res != NULL) {
		char *buf = ((char *)res) + sizeof(struct chx_string);
		memcpy(buf, value, len);
		buf[len] = '\0';

		res->value = buf;
		res->len = len;
		res->orig = res;
	}
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
		cry(c, "substr", CHEAX_EINDEX, "substring out of bounds");
		return NULL;
	}

	struct chx_string *res = gcol_alloc(c, sizeof(struct chx_string), CHEAX_STRING);
	if (res != NULL) {
		res->value = str->value + pos;
		res->len = len;
		res->orig = str->orig;
	}
	return res;
}
char *
cheax_strdup(struct chx_string *str)
{
	if (str == NULL)
		return NULL;

	char *res = malloc(str->len + 1);
	if (res != NULL) {
		res[str->len] = '\0';
		memcpy(res, str->value, str->len);
	}
	return res;
}

CHEAX *
cheax_init(void)
{
	CHEAX *res = malloc(sizeof(struct cheax));
	if (res == NULL)
		return NULL;

	res->globals.base.type = CHEAX_ENV;
	res->globals.base.rtflags = 0;
	norm_env_init(res, &res->globals, NULL);
	res->env = NULL;
	res->stack_depth = 0;

	res->features = 0;
	res->allow_redef = false;
	res->gen_debug_info = true;
	res->mem_limit = 0;
	res->stack_limit = 0;
	res->error.state = CHEAX_RUNNING;
	res->error.code = 0;
	res->error.msg = NULL;

	bt_init(res, 32);

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

	export_bltns(res);
	config_init(res);
	return res;
}
void
cheax_destroy(CHEAX *c)
{
	for (size_t i = 0; i < c->typestore.len; ++i) {
		struct type_cast *cnext, *cast;
		for (cast = c->typestore.array[i].casts; cast != NULL; cast = cnext) {
			cnext = cast->next;
			cheax_free(c, cast);
		}
	}

	cheax_free(c, c->typestore.array);
	cheax_free(c, c->user_error_names.array);
	free(c->config_syms);

	norm_env_cleanup(&c->globals);
	gcol_destroy(c);

	free(c);
}
const char *
cheax_version(void)
{
	return VERSION_STRING;
}

int
cheax_list_to_array(CHEAX *c, struct chx_list *list, struct chx_value ***array_ptr, size_t *length)
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
			struct chx_value **new_res = cheax_realloc(c, res, sizeof(*res) * cap);
			if (new_res == NULL) {
				cheax_free(c, res);
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

struct chx_list *
cheax_array_to_list(CHEAX *c, struct chx_value **array, size_t length)
{
	if (array == NULL && length > 0) {
		cry(c, "array_to_list", CHEAX_EAPI, "`array' cannot be NULL");
		return NULL;
	}

	struct chx_list *res = NULL;
	for (size_t i = length; i >= 1; --i) {
		res = cheax_list(c, array[i - 1], res);
		cheax_ft(c, pad);
	}

	return res;
pad:
	return NULL;
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

	struct chx_value *cpy = gcol_alloc(c, size, act_type);
	if (cpy != NULL) {
		unsigned short prev_rtflags = cpy->rtflags;
		memcpy(cpy, v, size);
		cpy->rtflags = prev_rtflags;
	}
	return cpy;
}

static bool
can_cast(CHEAX *c, struct chx_value *v, int type)
{
	if (!cheax_is_valid_type(c, type))
		return false;

	int vtype = cheax_type_of(v);
	return vtype == type
	    || cheax_get_base_type(c, vtype) == type;
}

struct chx_value *
cheax_cast(CHEAX *c, struct chx_value *v, int type)
{
	if (!can_cast(c, v, type)) {
		cry(c, "cast", CHEAX_ETYPE, "invalid cast");
		return NULL;
	}

	struct chx_value *res = cheax_shallow_copy(c, v);
	if (res != NULL)
		res->type = type;
	return res;
}

int
cheax_type_of(struct chx_value *v)
{
	return (v == NULL) ? CHEAX_NIL : v->type;
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

	int ts_idx = c->typestore.len;
	int tycode = ts_idx + CHEAX_TYPESTORE_BIAS;
	if (tycode > USHRT_MAX) {
		cry(c, "cheax_new_type", CHEAX_EEVAL, "too many types in existence", name);
		return -1;
	}

	if (c->typestore.len + 1 > c->typestore.cap) {
		size_t new_len, new_cap;
		new_len = c->typestore.len + 1;
		new_cap = new_len + (new_len / 2);


		void *new_array = cheax_realloc(c,
		                                c->typestore.array,
		                                new_cap * sizeof(struct type_alias));
		if (new_array == NULL)
			return -1;

		c->typestore.array = new_array;
		c->typestore.cap = new_cap;
	}

	cheax_def(c, name, &typecode(c, tycode)->base, CHEAX_READONLY);
	cheax_ft(c, pad);

	struct type_alias alias = { 0 };
	alias.name = name;
	alias.base_type = base_type;
	alias.print = NULL;
	alias.casts = NULL;
	c->typestore.array[ts_idx] = alias;
	++c->typestore.len;

	return tycode;
pad:
	return -1;
}
int
cheax_find_type(CHEAX *c, const char *name)
{
	if (name == NULL) {
		cry(c, "cheax_find_type", CHEAX_EAPI, "`name' cannot be NULL");
		return -1;
	}

	for (size_t i = 0; i < c->typestore.len; ++i)
		if (0 == strcmp(name, c->typestore.array[i].name))
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

int
cheax_load_prelude(CHEAX *c)
{
	static const char path[] = CMAKE_INSTALL_PREFIX "/share/cheax/prelude.chx";
	cheax_exec(c, path);
	return (cheax_errno(c) == 0) ? 0 : -1;
}

/*
 *  _           _ _ _   _
 * | |__  _   _(_) | |_(_)_ __  ___
 * | '_ \| | | | | | __| | '_ \/ __|
 * | |_) | |_| | | | |_| | | | \__ \
 * |_.__/ \__,_|_|_|\__|_|_| |_|___/
 *
 */

static struct chx_list *
prepend(CHEAX *c, struct chx_list *args)
{
	if (args->next != NULL) {
		struct chx_value *head = cheax_eval(c, args->value);
		cheax_ft(c, pad);
		chx_ref head_ref = cheax_ref(c, head);
		struct chx_list *tail = prepend(c, args->next);
		cheax_unref(c, head, head_ref);
		cheax_ft(c, pad);
		return cheax_list(c, head, tail);
	}

	struct chx_value *res = cheax_eval(c, args->value);
	cheax_ft(c, pad);
	int ty = cheax_type_of(res);
	if (ty != CHEAX_LIST && ty != CHEAX_NIL) {
		cry(c, ":", CHEAX_ETYPE, "improper list not allowed");
		return NULL;
	}

	return (struct chx_list *)res;
pad:
	return NULL;
}

static struct chx_value *
bltn_prepend(CHEAX *c, struct chx_list *args, void *info)
{
	if (args == NULL) {
		cry(c, ":", CHEAX_EMATCH, "expected at least one argument");
		return NULL;
	}

	return &prepend(c, args)->base;
}

static struct chx_value *
bltn_type_of(CHEAX *c, struct chx_list *args, void *info)
{
	struct chx_value *val;
	return (0 == unpack(c, "type-of", args, ".", &val))
	     ? &typecode(c, cheax_type_of(val))->base
	     : NULL;
}

static struct chx_value *
create_func(CHEAX *c,
            const char *name,
            struct chx_list *args,
            int type)
{
	if (args == NULL) {
		cry(c, name, CHEAX_EMATCH, "expected arguments");
		return NULL;
	}

	struct chx_value *arg_list = args->value;
	struct chx_list *body = args->next;

	if (body == NULL) {
		cry(c, name, CHEAX_EMATCH, "expected body");
		return NULL;
	}

	struct chx_func *res = gcol_alloc(c, sizeof(struct chx_func), type);
	if (res != NULL) {
		res->args = arg_list;
		res->body = body;
		res->lexenv = c->env;
	}
	return &res->base;
}

static struct chx_value *
bltn_fn(CHEAX *c, struct chx_list *args, void *info)
{
	return create_func(c, "fn", args, CHEAX_FUNC);
}

static struct chx_value *
bltn_macro(CHEAX *c, struct chx_list *args, void *info)
{
	return create_func(c, "macro", args, CHEAX_MACRO);
}

static struct chx_value *
bltn_strbytes(CHEAX *c, struct chx_list *args, void *info)
{
	struct chx_string *str;
	if (unpack(c, "strbytes", args, "s", &str) < 0)
		return NULL;

	struct chx_list *bytes = NULL;
	for (int i = (int)str->len - 1; i >= 0; --i)
		bytes = cheax_list(c, &cheax_int(c, (unsigned char)str->value[i])->base, bytes);
	return &bytes->base;
}

static struct chx_value *
bltn_strsize(CHEAX *c, struct chx_list *args, void *info)
{
	struct chx_string *str;
	return (0 == unpack(c, "strsize", args, "s", &str))
	     ? &cheax_int(c, (int)str->len)->base
	     : NULL;
}

static struct chx_value *
bltn_substr(CHEAX *c, struct chx_list *args, void *info)
{
	struct chx_string *str;
	int pos, len = 0;
	struct chx_int *len_or_nil;
	if (unpack(c, "substr", args, "si!i?", &str, &pos, &len_or_nil) < 0)
		return NULL;

	if (len_or_nil != NULL)
		len = len_or_nil->value;
	else if (pos >= 0 && (size_t)pos <= str->len)
		len = str->len - (size_t)pos;

	if (pos < 0 || len < 0) {
		cry(c, "substr", CHEAX_EVALUE, "expected positive integer");
		return NULL;
	}

	return &cheax_substr(c, str, pos, len)->base;
}

void
export_core_bltns(CHEAX *c)
{
	cheax_defmacro(c, ":",        bltn_prepend,  NULL);
	cheax_defmacro(c, "type-of",  bltn_type_of,  NULL);
	cheax_defmacro(c, "fn",       bltn_fn,       NULL);
	cheax_defmacro(c, "macro",    bltn_macro,    NULL);
	cheax_defmacro(c, "strbytes", bltn_strbytes, NULL);
	cheax_defmacro(c, "strsize",  bltn_strsize,  NULL);
	cheax_defmacro(c, "substr",   bltn_substr,   NULL);

	cheax_def(c, "cheax-version", &cheax_string(c, cheax_version())->base, CHEAX_READONLY);
}
