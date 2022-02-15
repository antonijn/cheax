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

struct variable *
find_sym(CHEAX *c, const char *name)
{
	for (struct variable *ht = c->locals_top; ht; ht = ht->below)
		if (!strcmp(name, ht->name))
			return ht;
	return NULL;
}

struct variable *
def_sym(CHEAX *c, const char *name, enum chx_varflags flags)
{
	struct variable *new = cheax_alloc_var(c);
	new->flags = flags;
	new->ctype = CTYPE_NONE;
	new->value.norm = NULL;
	new->name = name;
	new->below = c->locals_top;
	c->locals_top = new;
	return new;
}

void
cheax_defmacro(CHEAX *c, char *id, chx_func_ptr perform)
{
	cheax_var(c, id, &cheax_ext_func(c, perform, id)->base, CHEAX_READONLY);
}

void
cheax_var(CHEAX *c, char *id, struct chx_value *value, enum chx_varflags flags)
{
	if (id == NULL) {
		cry(c, "var", CHEAX_EAPI, "`id' cannot be NULL");
		return;
	}

	struct variable *sym = def_sym(c, id, flags & ~CHEAX_SYNCED);
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
cheax_set(CHEAX *c, char *id, struct chx_value *value)
{
	if (id == NULL) {
		cry(c, "set", CHEAX_EAPI, "`id' cannot be NULL");
		return;
	}

	struct variable *sym = find_sym(c, id);
	if (sym == NULL) {
		cry(c, "set", CHEAX_ENOSYM, "No such symbol \"%s\"", id);
		return;
	}

	if (sym->flags & CHEAX_READONLY) {
		cry(c, "set", CHEAX_EREADONLY, "Cannot write to read-only variable");
		return;
	}

	if ((sym->flags & CHEAX_SYNCED) == 0) {
		sym->value.norm = value;
		return;
	}

	switch (sym->ctype) {
	case CTYPE_INT:
		if (!try_convert_to_int(value, sym->value.sync_int)) {
			cry(c, "set", CHEAX_ETYPE, "Invalid type");
			return;
		}
		break;

	case CTYPE_FLOAT:
		; double d;
		if (!try_convert_to_double(value, &d)) {
			cry(c, "set", CHEAX_ETYPE, "Invalid type");
			return;
		}
		*sym->value.sync_float = d;
		break;

	case CTYPE_DOUBLE:
		if (!try_convert_to_double(value, sym->value.sync_double)) {
			cry(c, "set", CHEAX_ETYPE, "Invalid type");
			return;
		}
		break;

	default:
		cry(c, "set", CHEAX_EEVAL, "Unexpected sync-type");
		return;
	}

pad:
	return;
}

struct chx_value *
cheax_get(CHEAX *c, char *id)
{
	if (id == NULL) {
		cry(c, "get", CHEAX_EAPI, "`id' cannot be NULL");
		return NULL;
	}

	struct variable *sym = find_sym(c, id);
	if (!sym) {
		cry(c, "get", CHEAX_ENOSYM, "No such symbol `%s'", id);
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
		cry(c, "get", CHEAX_EEVAL, "Unexpected sync-type");
		return NULL;
	}
}


struct chx_quote *
cheax_quote(CHEAX *c, struct chx_value *value)
{
	struct chx_quote *res = cheax_alloc(c, sizeof(struct chx_quote));
	res->base.type = CHEAX_QUOTE;
	res->value = value;
	return res;
}
struct chx_quote *
cheax_backquote(CHEAX *c, struct chx_value *value)
{
	struct chx_quote *res = cheax_alloc(c, sizeof(struct chx_quote));
	res->base.type = CHEAX_BACKQUOTE;
	res->value = value;
	return res;
}
struct chx_quote *
cheax_comma(CHEAX *c, struct chx_value *value)
{
	struct chx_quote *res = cheax_alloc(c, sizeof(struct chx_quote));
	res->base.type = CHEAX_COMMA;
	res->value = value;
	return res;
}
struct chx_int *
cheax_int(CHEAX *c, int value)
{
	struct chx_int *res = cheax_alloc(c, sizeof(struct chx_int));
	res->base.type = CHEAX_INT;
	res->value = value;
	return res;
}
struct chx_double *
cheax_double(CHEAX *c, double value)
{
	struct chx_double *res = cheax_alloc(c, sizeof(struct chx_double));
	res->base.type = CHEAX_DOUBLE;
	res->value = value;
	return res;
}
struct chx_user_ptr *
cheax_user_ptr(CHEAX *c, void *value, int type)
{
	if (cheax_is_basic_type(c, type) || cheax_resolve_type(c, type) != CHEAX_USER_PTR) {
		cry(c, "cheax_user_ptr", CHEAX_EAPI, "Invalid user pointer type");
		return NULL;
	}
	struct chx_user_ptr *res = cheax_alloc(c, sizeof(struct chx_user_ptr));
	res->base.type = type;
	res->value = value;
	return res;
}
struct chx_id *
cheax_id(CHEAX *c, char *id)
{
	if (id == NULL)
		return NULL;

	/* NOTE: the GC depends on chx_id having this memory layout */
	struct chx_id *res = cheax_alloc(c, sizeof(struct chx_id) + strlen(id) + 1);
	char *buf = ((char *)res) + sizeof(struct chx_id);
	strcpy(buf, id);

	res->base.type = CHEAX_ID;
	res->id = buf;

	return res;
}
struct chx_list *
cheax_list(CHEAX *c, struct chx_value *car, struct chx_list *cdr)
{
	struct chx_list *res = cheax_alloc(c, sizeof(struct chx_list));
	res->base.type = CHEAX_LIST;
	res->value = car;
	res->next = cdr;
	return res;
}
struct chx_ext_func *
cheax_ext_func(CHEAX *c, chx_func_ptr perform, const char *name)
{
	if (perform == NULL || name == NULL)
		return NULL;

	struct chx_ext_func *res = cheax_alloc(c, sizeof(struct chx_ext_func));
	res->base.type = CHEAX_EXT_FUNC;
	res->perform = perform;
	res->name = name;
	return res;
}
struct chx_string *
cheax_string(CHEAX *c, char *value)
{
	if (value == NULL) {
		cry(c, "cheax_string", CHEAX_EAPI, "`value' cannot be NULL");
		return NULL;
	}

	return cheax_nstring(c, value, strlen(value));
}
struct chx_string *
cheax_nstring(CHEAX *c, char *value, size_t len)
{
	if (value == NULL) {
		if (len == 0) {
			value = "";
		} else {
			cry(c, "cheax_nstring", CHEAX_EAPI, "`value' cannot be NULL");
			return NULL;
		}
	}

	struct chx_string *res = cheax_alloc(c, sizeof(struct chx_string) + len + 1);
	char *buf = ((char *)res) + sizeof(struct chx_string);
	memcpy(buf, value, len);
	buf[len] = '\0';

	res->base.type = CHEAX_STRING;
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
			cry(c, "errname", CHEAX_EAPI, "Invalid user error code");
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

	cry(c, "errname", CHEAX_EAPI, "Invalid error code");
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
		cry(c, "throw", CHEAX_EAPI, "Cannot throw error code 0");
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
	errcode->type = CHEAX_ERRORCODE;
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
		errcode->type = CHEAX_ERRORCODE;
		cheax_var(c, name, errcode, CHEAX_READONLY);
	}
}

static bool pan_match_cheax_list(CHEAX *c,
                                 struct chx_list *pan,
                                 struct chx_list *match);

bool
cheax_match(CHEAX *c, struct chx_value *pan, struct chx_value *match)
{
	if (pan == NULL)
		return match == NULL;
	if (cheax_type_of(pan) == CHEAX_ID) {
		/* don't worry that "pan" will be wrongfully flagged as
		 * garbage, the GC detects that this string is from a
		 * chx_id. */
		cheax_var(c, ((struct chx_id *)pan)->id, match, 0);
		return true;
	}
	if (match == NULL)
		return false;
	if (pan->type == CHEAX_INT) {
		if (match->type != CHEAX_INT)
			return false;
		return ((struct chx_int *)pan)->value == ((struct chx_int *)match)->value;
	}
	if (pan->type == CHEAX_DOUBLE) {
		if (match->type != CHEAX_DOUBLE)
			return false;
		return ((struct chx_double *)pan)->value == ((struct chx_double *)match)->value;
	}
	if (pan->type == CHEAX_LIST) {
		struct chx_list *pan_list = (struct chx_list *)pan;
		if (match->type != CHEAX_LIST)
			return false;
		struct chx_list *match_list = (struct chx_list *)match;

		return pan_match_cheax_list(c, pan_list, match_list);
	}
	return false;
}

static bool
pan_match_colon_cheax_list(CHEAX *c,
                           struct chx_list *pan,
                           struct chx_list *match)
{
	if (!pan->next)
		return cheax_match(c, pan->value, &match->base);
	if (!match)
		return false;
	if (!cheax_match(c, pan->value, match->value))
		return false;
	return pan_match_colon_cheax_list(c, pan->next, match->next);
}

static bool
pan_match_cheax_list(CHEAX *c, struct chx_list *pan, struct chx_list *match)
{
	if (cheax_type_of(pan->value) == CHEAX_ID
	 && !strcmp((((struct chx_id *)pan->value)->id), ":"))
	{
		return pan_match_colon_cheax_list(c, pan->next, match);
	}

	while (pan && match) {
		if (!cheax_match(c, pan->value, match->value))
			return false;

		pan = pan->next;
		match = match->next;
	}

	return (pan == NULL) && (match == NULL);
}

bool
cheax_equals(CHEAX *c, struct chx_value *l, struct chx_value *r)
{
	if (cheax_type_of(l) != cheax_type_of(r))
		return false;

	int ty = cheax_resolve_type(c, cheax_type_of(l));

	switch (ty) {
	case CHEAX_NIL:
		return true;
	case CHEAX_ID:
		return !strcmp(((struct chx_id *)l)->id, ((struct chx_id *)r)->id);
	case CHEAX_INT:
		return ((struct chx_int *)l)->value == ((struct chx_int *)r)->value;
	case CHEAX_DOUBLE:
		return ((struct chx_double *)l)->value == ((struct chx_double *)r)->value;
	case CHEAX_LIST:
		;
		struct chx_list *llist = (struct chx_list *)l;
		struct chx_list *rlist = (struct chx_list *)r;
		return cheax_equals(c, llist->value, rlist->value)
		    && cheax_equals(c, &llist->next->base, &rlist->next->base);
	case CHEAX_EXT_FUNC:
		return ((struct chx_ext_func *)l)->perform == ((struct chx_ext_func *)r)->perform;
	case CHEAX_QUOTE:
	case CHEAX_BACKQUOTE:
	case CHEAX_COMMA:
		return cheax_equals(c, ((struct chx_quote *)l)->value, ((struct chx_quote *)r)->value);
	case CHEAX_STRING:
		;
		struct chx_string *lstring = (struct chx_string *)l;
		struct chx_string *rstring = (struct chx_string *)r;
		return (lstring->len == rstring->len) && !strcmp(lstring->value, rstring->value);
	case CHEAX_USER_PTR:
		return ((struct chx_user_ptr *)l)->value == ((struct chx_user_ptr *)r)->value;
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
	res->locals_top = NULL;
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
		for (struct type_cast *cast = c->typestore.array[i].casts; cast; cast = cnext) {
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
		cry(c, "cheax_set_max_stack_depth", CHEAX_EAPI, "Maximum stack depth must be positive");
}


struct chx_value *
cheax_shallow_copy(CHEAX *c, struct chx_value *v)
{
	int type = cheax_resolve_type(c, cheax_type_of(v));

	size_t size;
	switch (type) {
	case CHEAX_NIL:
		return NULL;
	case CHEAX_ID:
		size = sizeof(struct chx_id);
		break;
	case CHEAX_INT:
		size = sizeof(struct chx_int);
		break;
	case CHEAX_DOUBLE:
		size = sizeof(struct chx_double);
		break;
	case CHEAX_LIST:
		size = sizeof(struct chx_list);
		break;
	case CHEAX_FUNC:
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
	}

	void *cpy = cheax_alloc(c, size);
	memcpy(cpy, v, size);

	return cpy;
}

struct chx_value *
cheax_cast(CHEAX *c, struct chx_value *v, int type)
{
	/* TODO: improve critria */
	if (cheax_resolve_type(c, cheax_type_of(v)) != cheax_resolve_type(c, type)) {
		cry(c, "cast", CHEAX_ETYPE, "Invalid cast");
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
	if (v == NULL)
		return CHEAX_NIL;

	return v->type;
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

	struct chx_int *tci = cheax_int(c, typecode);
	tci->base.type = CHEAX_TYPECODE;
	cheax_var(c, name, &tci->base, CHEAX_READONLY);

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

	if (cheax_is_basic_type(c, type))
		return true;

	return (type - CHEAX_TYPESTORE_BIAS) < c->typestore.len;
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
		cry(c, "cheax_get_base_type", CHEAX_EEVAL, "Unable to resolve type");
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
			cry(c, "cheax_resolve_type", CHEAX_EEVAL, "Unable to resolve type");
			return -1;
		}

		type = base_type;
	}

	return type;
}

void
cheax_sync_int(CHEAX *c, const char *name, int *var, enum chx_varflags flags)
{
	struct variable *newsym = def_sym(c, name, flags | CHEAX_SYNCED);
	newsym->ctype = CTYPE_INT;
	newsym->value.sync_int = var;
}
void
cheax_sync_float(CHEAX *c, const char *name, float *var, enum chx_varflags flags)
{
	struct variable *newsym = def_sym(c, name, flags | CHEAX_SYNCED);
	newsym->ctype = CTYPE_FLOAT;
	newsym->value.sync_float = var;
}
void
cheax_sync_double(CHEAX *c, const char *name, double *var, enum chx_varflags flags)
{
	struct variable *newsym = def_sym(c, name, flags | CHEAX_SYNCED);
	newsym->ctype = CTYPE_DOUBLE;
	newsym->value.sync_double = var;
}

int
cheax_load_prelude(CHEAX *c)
{
	const char *path = CMAKE_INSTALL_PREFIX "/share/cheax/prelude.chx";
	FILE *f = fopen(path, "rb");
	if (!f) {
		cry(c, "cheax_load_prelude", CHEAX_EAPI, "Prelude not found at '%s'", path);
		return -1;
	}

	cheax_exec(c, f);
	fclose(f);

	if (cheax_errno(c) != 0)
		return -1;

	return 0;
}

