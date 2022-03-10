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
#include "setup.h"
#include "gc.h"

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
	if (res != NULL) {
		res->base.type = CHEAX_COMMA;
		res->value = value;
	}
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
		cry(c, "substr", CHEAX_EVALUE, "substring out of bounds");
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
		return cheax_errstate(c) != CHEAX_THROWN; /* false if cheax_var() failed */
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
	struct chx_string *msg = NULL;

	va_start(ap, frmt);
	size_t msglen = preamble_len + vsnprintf(NULL, 0, frmt, ap);
	va_end(ap);

	char *buf = malloc(msglen + 1);
	if (buf != NULL) {
		sprintf(buf, "(%s): ", name);
		va_start(ap, frmt);
		vsnprintf(buf + preamble_len, msglen - preamble_len + 1, frmt, ap);
		va_end(ap);

		/* hack to avoid allocation failure */
		int prev_mem_limit = c->mem_limit;
		c->mem_limit = 0;

		msg = cheax_nstring(c, buf, msglen);

		c->mem_limit = prev_mem_limit;
		free(buf);
	}

	cheax_throw(c, err, msg);
}

CHEAX *
cheax_init(void)
{
	CHEAX *res = malloc(sizeof(struct cheax));
	if (res == NULL)
		return NULL;

	res->globals.base.type = CHEAX_ENV | NO_GC_BIT;
	norm_env_init(res, &res->globals, NULL);
	res->env = NULL;
	res->stack_depth = 0;

	res->features = 0;
	res->allow_redef = false;
	res->mem_limit = 0;
	res->stack_limit = 0;
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

	export_builtins(res);
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
		int was_type = cpy->type;
		memcpy(cpy, v, size);
		cpy->type = was_type;
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
		c->typestore.array = cheax_realloc(c, c->typestore.array,
		                                   c->typestore.cap * sizeof(struct type_alias));
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
