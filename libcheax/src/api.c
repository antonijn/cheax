#include <cheax.h>
#include <gc.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>

#include "api.h"
#include "builtins.h"

struct variable *find_sym(CHEAX *c, const char *name)
{
	for (struct variable *ht = c->locals_top; ht; ht = ht->below)
		if (!strcmp(name, ht->name))
			return ht;
	return NULL;
}

struct variable *def_sym(CHEAX *c, const char *name, enum chx_varflags flags)
{
	struct variable *new = GC_MALLOC(sizeof(struct variable));
	new->flags = flags;
	new->ctype = CTYPE_NONE;
	new->value.norm = NULL;
	new->name = name;
	new->below = c->locals_top;
	c->locals_top = new;
	return new;
}

void cheax_defmacro(CHEAX *c, char *id, chx_func_ptr perform)
{
	cheax_var(c, id, &cheax_ext_func(c, perform, id)->base, CHEAX_READONLY);
}

void cheax_var(CHEAX *c, char *id, struct chx_value *value, enum chx_varflags flags)
{
	if (id == NULL) {
		cry(c, "var", CHEAX_EAPI, "`id' cannot be NULL");
		return;
	}

	struct variable *sym = def_sym(c, id, flags & ~CHEAX_SYNCED);
	sym->value.norm = value;
}


bool try_convert_to_double(struct chx_value *value, double *res)
{
	switch (cheax_get_type(value)) {
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
bool try_convert_to_int(struct chx_value *value, int *res)
{
	switch (cheax_get_type(value)) {
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


void cheax_set(CHEAX *c, char *id, struct chx_value *value)
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

struct chx_value *cheax_get(CHEAX *c, char *id)
{
	if (id == NULL) {
		cry(c, "get", CHEAX_EAPI, "`id' cannot be NULL");
		return NULL;
	}

	struct variable *sym = find_sym(c, id);
	if (!sym) {
		cry(c, "get", CHEAX_ENOSYM, "No such symbol '%s'", id);
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


struct chx_quote *cheax_quote(CHEAX *c, struct chx_value *value)
{
	struct chx_quote *res = GC_MALLOC(sizeof(struct chx_quote));
	res->base.type = CHEAX_QUOTE;
	res->value = value;
	return res;
}
struct chx_int *cheax_int(CHEAX *c, int value)
{
	struct chx_int *res = GC_MALLOC(sizeof(struct chx_int));
	res->base.type = CHEAX_INT;
	res->value = value;
	return res;
}
struct chx_double *cheax_double(CHEAX *c, double value)
{
	struct chx_double *res = GC_MALLOC(sizeof(struct chx_double));
	res->base.type = CHEAX_DOUBLE;
	res->value = value;
	return res;
}
struct chx_user_ptr *cheax_user_ptr(CHEAX *c, void *value, int type)
{
	if (!cheax_is_user_type(c, type)) {
		cry(c, "cheax_user_ptr", CHEAX_EAPI, "Invalid user pointer type");
		return NULL;
	}
	struct chx_user_ptr *res = GC_MALLOC(sizeof(struct chx_user_ptr));
	res->base.type = type;
	res->value = value;
	return res;
}
struct chx_id *cheax_id(CHEAX *c, char *id)
{
	if (id == NULL)
		return NULL;

	struct chx_id *res = GC_MALLOC(sizeof(struct chx_id) + strlen(id) + 1);
	char *buf = ((char *)res) + sizeof(struct chx_id);
	strcpy(buf, id);

	res->base.type = CHEAX_ID;
	res->id = buf;

	return res;
}
struct chx_list *cheax_list(CHEAX *c, struct chx_value *car, struct chx_list *cdr)
{
	struct chx_list *res = GC_MALLOC(sizeof(struct chx_list));
	res->base.type = CHEAX_LIST;
	res->value = car;
	res->next = cdr;
	return res;
}
struct chx_ext_func *cheax_ext_func(CHEAX *c, chx_func_ptr perform, const char *name)
{
	if (perform == NULL || name == NULL)
		return NULL;

	struct chx_ext_func *res = GC_MALLOC(sizeof(struct chx_ext_func));
	res->base.type = CHEAX_EXT_FUNC;
	res->perform = perform;
	res->name = name;
	return res;
}
struct chx_string *cheax_string(CHEAX *c, char *value)
{
	if (value == NULL)
		return NULL;

	return cheax_nstring(c, value, strlen(value));
}
struct chx_string *cheax_nstring(CHEAX *c, char *value, size_t len)
{
	if (value == NULL)
		return NULL;

	struct chx_string *res = GC_MALLOC(sizeof(struct chx_string) + len + 1);
	char *buf = ((char *)res) + sizeof(struct chx_string);
	memcpy(buf, value, len + 1);

	res->base.type = CHEAX_STRING;
	res->value = buf;
	res->len = len;

	return res;
}

int cheax_errno(CHEAX *c)
{
	return c->error.code;
}
static const char *errname(int code)
{
	switch (code) {
	case CHEAX_EREAD:      return "EREAD";
	case CHEAX_EEOF:       return "EEOF";
	case CHEAX_ELEX:       return "ELEX";
	case CHEAX_EEVAL:      return "EEVAL";
	case CHEAX_ENOSYM:     return "ENOSYM";
	case CHEAX_ESTACK:     return "ESTACK";
	case CHEAX_ETYPE:      return "ETYPE";
	case CHEAX_EMATCH:     return "EMATCH";
	case CHEAX_ENIL:       return "ENIL";
	case CHEAX_EDIVZERO:   return "EDIVZERO";
	case CHEAX_EREADONLY:  return "EREADONLY";
	case CHEAX_EVALUE:     return "EVALUE";
	case CHEAX_EOVERFLOW:  return "EOVERFLOW";
	case CHEAX_EAPI:       return "EAPI";
	}

	return NULL;
}
void cheax_perror(CHEAX *c, const char *s)
{
	int err = cheax_errno(c);
	if (err == 0)
		return;

	if (s != NULL)
		fprintf(stderr, "%s: ", s);

	if (c->error.msg != NULL)
		fprintf(stderr, "%s ", c->error.msg->value);

	const char *ename = errname(err);
	if (ename != NULL)
		fprintf(stderr, "(CHEAX error %s)", ename);
	else
		fprintf(stderr, "(CHEAX error code %x)", err);

	fprintf(stderr, "\n");
}
void cheax_clear_errno(CHEAX *c)
{
	c->error.code = 0;
	c->error.msg = NULL;
}

static bool pan_match_cheax_list(CHEAX *c, struct chx_list *pan, struct chx_list *match);

bool cheax_match(CHEAX *c, struct chx_value *pan, struct chx_value *match)
{
	if (pan == NULL)
		return match == NULL;
	if (cheax_get_type(pan) == CHEAX_ID) {
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

static bool pan_match_colon_cheax_list(CHEAX *c, struct chx_list *pan, struct chx_list *match)
{
	if (!pan->next)
		return cheax_match(c, pan->value, &match->base);
	if (!match)
		return false;
	if (!cheax_match(c, pan->value, match->value))
		return false;
	return pan_match_colon_cheax_list(c, pan->next, match->next);
}

static bool pan_match_cheax_list(CHEAX *c, struct chx_list *pan, struct chx_list *match)
{
	if (cheax_get_type(pan->value) == CHEAX_ID
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

bool cheax_equals(CHEAX *c, struct chx_value *l, struct chx_value *r)
{
	if (cheax_get_type(l) != cheax_get_type(r))
		return false;

	int ty = cheax_get_type(l);
	if (cheax_is_user_type(c, ty))
		return ((struct chx_user_ptr *)l)->value == ((struct chx_user_ptr *)r)->value;

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
		return cheax_equals(c, ((struct chx_quote *)l)->value, ((struct chx_quote *)r)->value);
	case CHEAX_STRING:
		;
		struct chx_string *lstring = (struct chx_string *)l;
		struct chx_string *rstring = (struct chx_string *)r;
		return (lstring->len == rstring->len) && !strcmp(lstring->value, rstring->value);

	default:
		return l == r;
	}
}

void cry(CHEAX *c, const char *name, int err, const char *frmt, ...)
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

	c->error.code = err;
	c->error.msg = cheax_nstring(c, buf, msglen);

	free(buf);
}

CHEAX *cheax_init(void)
{
	CHEAX *res = malloc(sizeof(struct cheax));
	res->locals_top = NULL;
	res->max_stack_depth = 0x1000;
	res->stack_depth = 0;
	res->user_type_count = 0;
	res->error.code = 0;
	res->error.msg = NULL;
	export_builtins(res);
	return res;
}
void cheax_destroy(CHEAX *c)
{
	free(c);
}

int cheax_get_max_stack_depth(CHEAX *c)
{
	return c->max_stack_depth;
}
void cheax_set_max_stack_depth(CHEAX *c, int max_stack_depth)
{
	if (max_stack_depth > 0)
		c->max_stack_depth = max_stack_depth;
	else
		cry(c, "cheax_set_max_stack_depth", CHEAX_EAPI, "Maximum stack depth must be positive");
}

int cheax_get_type(struct chx_value *v)
{
	if (v == NULL)
		return CHEAX_NIL;

	return v->type;
}
int cheax_new_user_type(CHEAX *c)
{
	return CHEAX_USER_TYPE + c->user_type_count++;
}
int cheax_is_user_type(CHEAX *c, int type)
{
	return type >= CHEAX_USER_TYPE;
}

void cheax_sync_int(CHEAX *c, const char *name, int *var, enum chx_varflags flags)
{
	struct variable *newsym = def_sym(c, name, flags | CHEAX_SYNCED);
	newsym->ctype = CTYPE_INT;
	newsym->value.sync_int = var;
}
void cheax_sync_float(CHEAX *c, const char *name, float *var, enum chx_varflags flags)
{
	struct variable *newsym = def_sym(c, name, flags | CHEAX_SYNCED);
	newsym->ctype = CTYPE_FLOAT;
	newsym->value.sync_float = var;
}
void cheax_sync_double(CHEAX *c, const char *name, double *var, enum chx_varflags flags)
{
	struct variable *newsym = def_sym(c, name, flags | CHEAX_SYNCED);
	newsym->ctype = CTYPE_DOUBLE;
	newsym->value.sync_double = var;
}

int cheax_load_prelude(CHEAX *c)
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

