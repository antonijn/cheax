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

bool
cheax_is_nil(struct chx_value v)
{
	return v.type == CHEAX_LIST && v.data.as_list == NULL;
}

struct chx_value
cheax_quote(CHEAX *c, struct chx_value value)
{
	struct chx_value res = cheax_quote_value(gc_alloc(c, sizeof(struct chx_quote), CHEAX_QUOTE));
	if (res.data.as_quote == NULL)
		return cheax_nil();
	res.data.as_quote->value = value;
	return res;
}
struct chx_value
cheax_quote_value_proc(struct chx_quote *quote)
{
	return cheax_quote_value(quote);
}

struct chx_value
cheax_backquote(CHEAX *c, struct chx_value value)
{
	struct chx_value res = cheax_backquote_value(gc_alloc(c, sizeof(struct chx_quote), CHEAX_BACKQUOTE));
	if (res.data.as_quote == NULL)
		return cheax_nil();
	res.data.as_quote->value = value;
	return res;
}
struct chx_value
cheax_backquote_value_proc(struct chx_quote *backquote)
{
	return cheax_backquote_value(backquote);
}

struct chx_value
cheax_comma(CHEAX *c, struct chx_value value)
{
	struct chx_value res = cheax_comma_value(gc_alloc(c, sizeof(struct chx_quote), CHEAX_COMMA));
	if (res.data.as_quote == NULL)
		return cheax_nil();
	res.data.as_quote->value = value;
	return res;
}
struct chx_value
cheax_comma_value_proc(struct chx_quote *comma)
{
	return cheax_comma_value(comma);
}

struct chx_value
cheax_splice(CHEAX *c, struct chx_value value)
{
	struct chx_value res = cheax_splice_value(gc_alloc(c, sizeof(struct chx_quote), CHEAX_SPLICE));
	if (res.data.as_quote == NULL)
		return cheax_nil();
	res.data.as_quote->value = value;
	return res;
}
struct chx_value
cheax_splice_value_proc(struct chx_quote *splice)
{
	return cheax_splice_value(splice);
}

struct chx_value
cheax_int_proc(chx_int value)
{
	return cheax_int(value);
}
struct chx_value
cheax_bool_proc(bool value)
{
	return cheax_bool(value);
}
struct chx_value
cheax_double_proc(chx_double value)
{
	return cheax_double(value);
}

struct chx_value
cheax_user_ptr(CHEAX *c, void *value, int type)
{
	if (cheax_is_basic_type(c, type) || cheax_resolve_type(c, type) != CHEAX_USER_PTR) {
		cheax_throwf(c, CHEAX_EAPI, "user_ptr(): invalid user pointer type");
		return cheax_nil();
	}
	return ((struct chx_value){ .type = type, .data.user_ptr = value });
}

struct chx_value
cheax_id(CHEAX *c, const char *id)
{
	if (id == NULL)
		return cheax_nil();

	size_t len = strlen(id) + 1;
	struct chx_value res = cheax_id_value(gc_alloc(c, sizeof(struct chx_id) + len, CHEAX_ID));
	if (res.data.as_id == NULL)
		return cheax_nil();
	char *buf = ((char *)res.data.as_id) + sizeof(struct chx_id);
	memcpy(buf, id, len);
	res.data.as_id->value = buf;
	return res;
}
struct chx_value
cheax_id_value_proc(struct chx_id *id)
{
	return cheax_id_value(id);
}

struct chx_value
cheax_list(CHEAX *c, struct chx_value car, struct chx_list *cdr)
{
	struct chx_value res = cheax_list_value(gc_alloc(c, sizeof(struct chx_list), CHEAX_LIST));
	if (res.data.as_list == NULL)
		return cheax_nil();
	res.data.as_list->value = car;
	res.data.as_list->next = cdr;
	return res;
}
struct chx_value
cheax_list_value_proc(struct chx_list *list)
{
	return cheax_list_value(list);
}

struct debug_list *
debug_list(CHEAX *c, struct chx_value car, struct chx_list *cdr, struct debug_info info)
{
	struct debug_list *res = gc_alloc(c, sizeof(struct debug_list), CHEAX_LIST);
	if (res == NULL)
		return NULL;
	res->base.rtflags |= DEBUG_LIST;
	res->base.value = car;
	res->base.next = cdr;
	res->info = info;
	return res;
}

struct chx_value
cheax_ext_func(CHEAX *c, const char *name, chx_func_ptr perform, void *info)
{
	if (perform == NULL || name == NULL)
		return cheax_nil();

	struct chx_value res = cheax_ext_func_value(gc_alloc(c, sizeof(struct chx_form), CHEAX_EXT_FUNC));
	if (res.data.as_form == NULL)
		return cheax_nil();
	res.data.as_form->name = name;
	res.data.as_form->perform.func = perform;
	res.data.as_form->info = info;
	return res;
}
struct chx_value
cheax_ext_func_value_proc(struct chx_form *extf)
{
	return cheax_ext_func_value(extf);
}

struct chx_value
cheax_special_form(CHEAX *c, const char *name, chx_func_ptr perform, void *info)
{
	if (perform == NULL || name == NULL)
		return cheax_nil();

	struct chx_value res = cheax_special_form_value(gc_alloc(c, sizeof(struct chx_form), CHEAX_SPECIAL_FORM));
	if (res.data.as_form == NULL)
		return cheax_nil();
	res.data.as_form->name = name;
	res.data.as_form->perform.func = perform;
	res.data.as_form->info = info;
	return res;
}
struct chx_value
cheax_special_form_value_proc(struct chx_form *extf)
{
	return cheax_special_form_value(extf);
}

struct chx_value
cheax_special_tail_form(CHEAX *c, const char *name, chx_tail_func_ptr perform, void *info)
{
	if (perform == NULL || name == NULL)
		return cheax_nil();

	struct chx_value res = cheax_special_tail_form_value(gc_alloc(c, sizeof(struct chx_form), CHEAX_SPECIAL_TAIL_FORM));
	if (res.data.as_form == NULL)
		return cheax_nil();
	res.data.as_form->name = name;
	res.data.as_form->perform.tail_func = perform;
	res.data.as_form->info = info;
	return res;
}
struct chx_value
cheax_special_tail_form_value_proc(struct chx_form *extf)
{
	return cheax_special_tail_form_value(extf);
}

size_t
cheax_strlen(CHEAX *c, struct chx_string *str)
{
	return (str == NULL) ? 0 : str->len;
}
struct chx_value
cheax_string(CHEAX *c, const char *value)
{
	ASSERT_NOT_NULL("string", value, cheax_nil());
	return cheax_nstring(c, value, strlen(value));
}
struct chx_value
cheax_nstring(CHEAX *c, const char *value, size_t len)
{
	if (value == NULL && len == 0)
		value = "";

	ASSERT_NOT_NULL("nstring", value, cheax_nil());

	struct chx_value res = cheax_string_value(gc_alloc(c, sizeof(struct chx_string) + len + 1, CHEAX_STRING));
	if (res.data.as_string == NULL)
		return cheax_nil();

	char *buf = ((char *)res.data.as_string) + sizeof(struct chx_string);
	memcpy(buf, value, len);
	buf[len] = '\0';

	res.data.as_string->value = buf;
	res.data.as_string->len = len;
	res.data.as_string->orig = res.data.as_string;
	return res;
}
struct chx_value
cheax_string_value_proc(struct chx_string *string)
{
	return cheax_string_value(string);
}
struct chx_value
cheax_substr(CHEAX *c, struct chx_string *str, size_t pos, size_t len)
{
	ASSERT_NOT_NULL("substr", str, cheax_nil());

	if (pos > SIZE_MAX - len || pos + len > str->len) {
		cheax_throwf(c, CHEAX_EINDEX, "substr(): substring out of bounds");
		return cheax_nil();
	}

	struct chx_value res;
	res.type = CHEAX_STRING;
	res.data.as_string = gc_alloc(c, sizeof(struct chx_string), CHEAX_STRING);
	if (res.data.as_string == NULL)
		return cheax_nil();

	res.data.as_string->value = str->value + pos;
	res.data.as_string->len = len;
	res.data.as_string->orig = str->orig;
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

struct chx_value
cheax_func_value_proc(struct chx_func *fn)
{
	return cheax_func_value(fn);
}
struct chx_value
cheax_macro_value_proc(struct chx_func *macro)
{
	return cheax_macro_value(macro);
}

struct chx_value
cheax_env_value_proc(struct chx_env *env)
{
	return cheax_env_value(env);
}

CHEAX *
cheax_init(void)
{
	CHEAX *res = malloc(sizeof(struct cheax));
	if (res == NULL)
		return NULL;

	res->globals.rtflags = 0;
	norm_env_init(res, &res->globals, NULL);
	res->env = NULL;
	res->stack_depth = 0;

	res->features = 0;
	res->allow_redef = false;
	res->gen_debug_info = true;
	res->tail_call_elimination = true;
	res->mem_limit = 0;
	res->stack_limit = 0;
	res->error.code = 0;
	res->error.msg = NULL;

	gc_init(res);
	bt_init(res, 32);

	res->typestore.array = NULL;
	res->typestore.len = res->typestore.cap = 0;

	res->user_error_names.array = NULL;
	res->user_error_names.len = res->user_error_names.cap = 0;

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

	gc_cleanup(c);
	norm_env_cleanup(&c->globals);

	cheax_free(c, c->bt.array);

	for (size_t i = 0; i < c->typestore.len; ++i)
		cheax_free(c, c->typestore.array[i].name);
	cheax_free(c, c->typestore.array);

	for (size_t i = 0; i < c->user_error_names.len; ++i)
		cheax_free(c, c->user_error_names.array[i]);
	cheax_free(c, c->user_error_names.array);

	free(c->config_syms);

	free(c);
}
const char *
cheax_version(void)
{
	return VERSION_STRING;
}

int
cheax_list_to_array(CHEAX *c, struct chx_list *list, struct chx_value **array_ptr, size_t *length)
{
	size_t len = 0, cap = 0;
	struct chx_value *res = NULL;

	ASSERT_NOT_NULL("list_to_array", length, -1);
	ASSERT_NOT_NULL("list_to_array", array_ptr, -1);

	for (; list != NULL; list = list->next) {
		if (++len > cap) {
			cap = len + len / 2;
			struct chx_value *new_res = cheax_realloc(c, res, sizeof(*res) * cap);
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

struct chx_value
cheax_array_to_list(CHEAX *c, struct chx_value *array, size_t length)
{
	ASSERT_NOT_NULL("array_to_list", array, cheax_nil());

	struct chx_value res = cheax_nil();
	for (size_t i = length; i >= 1; --i) {
		res = cheax_list(c, array[i - 1], res.data.as_list);
		cheax_ft(c, pad);
	}

	return res;
pad:
	return cheax_nil();
}

static bool
can_cast(CHEAX *c, struct chx_value v, int type)
{
	if (!cheax_is_valid_type(c, type))
		return false;

	return v.type == type
	    || cheax_get_base_type(c, v.type) == type;
}

struct chx_value
cheax_cast(CHEAX *c, struct chx_value v, int type)
{
	if (!can_cast(c, v, type)) {
		cheax_throwf(c, CHEAX_ETYPE, "cast(): invalid cast");
		return cheax_nil();
	}

	v.type = type;
	return v;
}

int
cheax_new_type(CHEAX *c, const char *name, int base_type)
{
	ASSERT_NOT_NULL("new_type", name, -1);

	if (!cheax_is_valid_type(c, base_type)) {
		cheax_throwf(c, CHEAX_EAPI, "new_type(): `base_type' is not a valid type");
		return -1;
	}

	if (cheax_find_type(c, name) != -1) {
		cheax_throwf(c, CHEAX_EAPI, "new_type(): `%s' already exists as a type", name);
		return -1;
	}

	int ts_idx = c->typestore.len;
	int tycode = ts_idx + CHEAX_TYPESTORE_BIAS;
	if (tycode > USHRT_MAX) {
		cheax_throwf(c, CHEAX_EEVAL, "new_type(): too many types in existence", name);
		return -1;
	}

	if (c->typestore.len + 1 > c->typestore.cap) {
		size_t new_len, new_cap;
		new_len = c->typestore.len + 1;
		new_cap = new_len + (new_len / 2);


		void *new_array = cheax_realloc(c,
		                                c->typestore.array,
		                                new_cap * sizeof(struct type_alias));
		cheax_ft(c, pad);

		c->typestore.array = new_array;
		c->typestore.cap = new_cap;
	}

	char *store_name = cheax_malloc(c, strlen(name) + 1);
	cheax_ft(c, pad);
	strcpy(store_name, name);

	cheax_def(c, name, typecode(tycode), CHEAX_READONLY);
	cheax_ft(c, pad);

	struct type_alias alias = { 0 };
	alias.name = store_name;
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
	ASSERT_NOT_NULL("find_type", name, -1);

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
		cheax_throwf(c, CHEAX_EEVAL, "get_base_type(): unable to resolve type");
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
			cheax_throwf(c, CHEAX_EEVAL, "resolve_type(): unable to resolve type");
			return -1;
		}

		type = base_type;
	}

	return type;
}

bool
try_vtoi(struct chx_value value, chx_int *res)
{
	switch (value.type) {
	case CHEAX_INT:
		*res = value.data.as_int;
		return true;
	case CHEAX_DOUBLE:
		*res = value.data.as_double;
		return true;
	default:
		return false;
	}
}
bool
try_vtod(struct chx_value value, chx_double *res)
{
	switch (value.type) {
	case CHEAX_INT:
		*res = value.data.as_int;
		return true;
	case CHEAX_DOUBLE:
		*res = value.data.as_double;
		return true;
	default:
		return false;
	}
}
double
vtod(struct chx_value value)
{
	double res = 0.0;
	try_vtod(value, &res);
	return res;
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

static struct chx_value
create_func(CHEAX *c,
            const char *name,
            struct chx_list *args,
            int type)
{
	if (args == NULL) {
		cheax_throwf(c, CHEAX_EMATCH, "expected arguments");
		return cheax_nil();
	}

	struct chx_value arg_list = args->value;
	struct chx_list *body = args->next;

	if (body == NULL) {
		cheax_throwf(c, CHEAX_EMATCH, "expected body");
		return cheax_nil();
	}

	struct chx_value res;
	res.type = type;
	res.data.as_func = gc_alloc(c, sizeof(struct chx_func), type);
	if (res.data.as_func == NULL)
		return cheax_nil();

	res.data.as_func->args = arg_list;
	res.data.as_func->body = body;
	res.data.as_func->lexenv = cheax_env(c).data.as_env;
	return res;
}

static struct chx_value
sf_fn(CHEAX *c, struct chx_list *args, void *info)
{
	return bt_wrap(c, create_func(c, "fn", args, CHEAX_FUNC));
}

static struct chx_value
sf_macro(CHEAX *c, struct chx_list *args, void *info)
{
	return bt_wrap(c, create_func(c, "macro", args, CHEAX_MACRO));
}

static struct chx_list *
prepend(CHEAX *c, struct chx_list *args)
{
	if (args->next != NULL) {
		struct chx_list *tail = prepend(c, args->next);
		cheax_ft(c, pad);
		return cheax_list(c, args->value, tail).data.as_list;
	}

	struct chx_value res = args->value;
	if (res.type != CHEAX_LIST) {
		cheax_throwf(c, CHEAX_ETYPE, "improper list not allowed");
		return NULL;
	}

	return res.data.as_list;
pad:
	return NULL;
}

static struct chx_value
bltn_prepend(CHEAX *c, struct chx_list *args, void *info)
{
	if (args == NULL) {
		cheax_throwf(c, CHEAX_EMATCH, "expected at least one argument");
		return bt_wrap(c, cheax_nil());
	}

	return bt_wrap(c, cheax_list_value(prepend(c, args)));
}

static struct chx_value
bltn_type_of(CHEAX *c, struct chx_list *args, void *info)
{
	struct chx_value val;
	return (0 == unpack(c, args, "_", &val))
	     ? bt_wrap(c, typecode(val.type))
	     : cheax_nil();
}

static struct chx_value
bltn_string_bytes(CHEAX *c, struct chx_list *args, void *info)
{
	struct chx_string *str;
	if (unpack(c, args, "S", &str) < 0)
		return cheax_nil();

	struct chx_value bytes = cheax_nil();
	for (size_t i = str->len; i >= 1; --i)
		bytes = cheax_list(c, cheax_int((unsigned char)str->value[i - 1]), bytes.data.as_list);
	return bt_wrap(c, bytes);
}

static struct chx_value
bltn_string_length(CHEAX *c, struct chx_list *args, void *info)
{
	struct chx_string *str;
	return (0 == unpack(c, args, "S", &str))
	     ? bt_wrap(c, cheax_int((chx_int)str->len))
	     : cheax_nil();
}

static struct chx_value
bltn_substr(CHEAX *c, struct chx_list *args, void *info)
{
	struct chx_string *str;
	chx_int pos, len = 0;
	struct chx_value len_or_nil;
	if (unpack(c, args, "SII?", &str, &pos, &len_or_nil) < 0)
		return cheax_nil();

	if (!cheax_is_nil(len_or_nil))
		len = len_or_nil.data.as_int;
	else if (pos >= 0 && (size_t)pos <= str->len)
		len = str->len - (size_t)pos;

	if (pos < 0 || len < 0) {
		cheax_throwf(c, CHEAX_EVALUE, "expected positive integer");
		return bt_wrap(c, cheax_nil());
	}

	return bt_wrap(c, cheax_substr(c, str, pos, len));
}

void
export_core_bltns(CHEAX *c)
{
	cheax_def_special_form(c, "fn",    sf_fn,    NULL);
	cheax_def_special_form(c, "macro", sf_macro, NULL);

	cheax_defun(c, ":",             bltn_prepend,       NULL);
	cheax_defun(c, "type-of",       bltn_type_of,       NULL);
	cheax_defun(c, "string-bytes",  bltn_string_bytes,  NULL);
	cheax_defun(c, "string-length", bltn_string_length, NULL);
	cheax_defun(c, "substr",        bltn_substr,        NULL);

	cheax_def(c, "cheax-version", cheax_string(c, cheax_version()), CHEAX_READONLY);
}
