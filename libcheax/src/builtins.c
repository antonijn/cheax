/* Copyright (c) 2020, Antonie Blom
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

#include "builtins.h"
#include "eval.h"

#include <gc.h>
#include <cheax.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DECL_BUILTIN(cname) \
static struct chx_value *builtin_##cname(CHEAX *c, struct chx_list *args)

static struct chx_int yes = { { CHEAX_INT }, 1 }, no = { { CHEAX_INT }, 0 };

DECL_BUILTIN(defmacro);
DECL_BUILTIN(fopen);
DECL_BUILTIN(fclose);
DECL_BUILTIN(read_from);
DECL_BUILTIN(print_to);
DECL_BUILTIN(getc);
DECL_BUILTIN(putc);
DECL_BUILTIN(var);
DECL_BUILTIN(const);
DECL_BUILTIN(set);
DECL_BUILTIN(prepend);
DECL_BUILTIN(get_type);
DECL_BUILTIN(get_max_stack_depth);
DECL_BUILTIN(set_max_stack_depth);
DECL_BUILTIN(lambda);
DECL_BUILTIN(macro_lambda);
DECL_BUILTIN(eval);
DECL_BUILTIN(case);
DECL_BUILTIN(add);
DECL_BUILTIN(sub);
DECL_BUILTIN(mul);
DECL_BUILTIN(div);
DECL_BUILTIN(mod);
DECL_BUILTIN(eq);
DECL_BUILTIN(lt);

void export_builtins(CHEAX *c)
{
	cheax_defmacro(c, "read-from", builtin_read_from);
	cheax_defmacro(c, "print-to", builtin_print_to);
	cheax_defmacro(c, "var", builtin_var);
	cheax_defmacro(c, "const", builtin_const);
	cheax_defmacro(c, "set", builtin_set);
	cheax_defmacro(c, ":", builtin_prepend);
	cheax_defmacro(c, "get-type", builtin_get_type);
	cheax_defmacro(c, "get-max-stack-depth", builtin_get_max_stack_depth);
	cheax_defmacro(c, "\\", builtin_lambda);
	cheax_defmacro(c, "\\\\", builtin_macro_lambda);
	cheax_defmacro(c, "eval", builtin_eval);
	cheax_defmacro(c, "case", builtin_case);
	cheax_defmacro(c, "+", builtin_add);
	cheax_defmacro(c, "-", builtin_sub);
	cheax_defmacro(c, "*", builtin_mul);
	cheax_defmacro(c, "/", builtin_div);
	cheax_defmacro(c, "%", builtin_mod);
	cheax_defmacro(c, "=", builtin_eq);
	cheax_defmacro(c, "<", builtin_lt);

	c->fhandle_type = cheax_new_user_type(c);
	cheax_decl_user_data(c, "stdin", stdin, c->fhandle_type);
	cheax_decl_user_data(c, "stdout", stdout, c->fhandle_type);
	cheax_decl_user_data(c, "stderr", stderr, c->fhandle_type);
}

void cheax_load_extra_builtins(CHEAX *c, enum cheax_builtin builtins)
{
	if (builtins & CHEAX_FILE_IO) {
		cheax_defmacro(c, "fopen", builtin_fopen);
		cheax_defmacro(c, "fclose", builtin_fclose);
	}

	if (builtins & CHEAX_SET_MAX_STACK_DEPTH)
		cheax_defmacro(c, "set-max-stack-depth", builtin_set_max_stack_depth);
}

static bool unpack_args(CHEAX *c, const char *fname, struct chx_list *args, int num, ...)
{
	va_list ap;
	va_start(ap, num);
	for (int i = 0; i < num; ++i) {
		if (args == NULL) {
			cry(c, fname, CHEAX_EMATCH, "Expected %d arguments (got %d)", num, i);
			return false;
		}

		struct chx_value **res = va_arg(ap, struct chx_value **);
		*res = cheax_evalp(c, args->value, pad);
		args = args->next;
	}

	if (args != NULL) {
		cry(c, fname, CHEAX_EMATCH, "Expected only %d arguments", num);
		return false;
	}

	return true;

pad:
	return false;
}

static bool try_convert_to_double(struct chx_value *value, double *res)
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
static bool try_convert_to_int(struct chx_value *value, int *res)
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


static struct chx_value *builtin_fopen(CHEAX *c, struct chx_list *args)
{
	struct chx_value *fname, *mode;
	if (!unpack_args(c, "fopen", args, 2, &fname, &mode))
		return NULL;

	if (cheax_get_type(fname) != CHEAX_STRING || cheax_get_type(mode) != CHEAX_STRING) {
		cry(c, "fopen", CHEAX_ETYPE, "Invalid argument type");
		return NULL;
	}

	const char *fname_str = ((struct chx_string *)fname)->value;
	const char *mode_str = ((struct chx_string *)mode)->value;
	FILE *f = fopen(fname_str, mode_str);
	struct chx_ptr *handle = GC_MALLOC(sizeof(struct chx_ptr));
	handle->base.type = c->fhandle_type;
	handle->ptr = f;

	return &handle->base;
}
static struct chx_value *builtin_fclose(CHEAX *c, struct chx_list *args)
{
	struct chx_value *handle;
	if (!unpack_args(c, "fclose", args, 1, &handle))
		return NULL;

	if (cheax_get_type(handle) != c->fhandle_type) {
		cry(c, "fclose", CHEAX_ETYPE, "Invalid argument type");
		return NULL;
	}

	FILE *f = (FILE *)((struct chx_ptr *)handle)->ptr;
	fclose(f);

	return NULL;
}
static struct chx_value *builtin_read_from(CHEAX *c, struct chx_list *args)
{
	struct chx_value *handle;
	if (!unpack_args(c, "read-from", args, 1, &handle))
		return NULL;

	if (cheax_get_type(handle) != c->fhandle_type) {
		cry(c, "fclose", CHEAX_ETYPE, "Invalid argument type");
		return NULL;
	}

	FILE *f = (FILE *)((struct chx_ptr *)handle)->ptr;
	return cheax_read(c, f);
}
static struct chx_value *builtin_print_to(CHEAX *c, struct chx_list *args)
{
	struct chx_value *handle;
	struct chx_value *value;
	if (!unpack_args(c, "print-to", args, 2, &handle, &value))
		return NULL;

	if (cheax_get_type(handle) != c->fhandle_type) {
		cry(c, "print-to", CHEAX_ETYPE, "Invalid argument type");
		return NULL;
	}

	FILE *f = (FILE *)((struct chx_ptr *)handle)->ptr;
	cheax_print(f, value);
	fputc('\n', f);
	return NULL;
}

static struct chx_value *builtin_const(CHEAX *c, struct chx_list *args)
{
	if (args == NULL) {
		cry(c, "const", CHEAX_EMATCH, "Invalid declaration");
		return NULL;
	}

	struct chx_value *idval = args->value;
	args = args->next;

	struct chx_value *setto = NULL;
	if (args != NULL) {
		setto = cheax_evalp(c, args->value, pad);
		args = args->next;
	}

	if (args != NULL) {
		cry(c, "const", CHEAX_EMATCH,  "Invalid declaration");
		return NULL;
	}

	struct variable *prev_top = c->locals_top;

	if (!pan_match(c, idval, setto)) {
		cry(c, "const", CHEAX_EMATCH, "Invalid pattern");
		return NULL;
	}

	if (setto && setto->type == CHEAX_FUNC) {
		/* to allow for recursion:
 		 * if this wasn't here, the function symbol would not have been
 		 * available to the lambda, and thus a recursive function would
 		 * give an undefined symbol error.
 		 */
		((struct chx_func *)setto)->locals_top = c->locals_top;
	}

	for (struct variable *v = c->locals_top; v != prev_top; v = v->below)
		v->flags |= CHEAX_READONLY;

pad:
	return NULL;
}

static struct chx_value *builtin_var(CHEAX *c, struct chx_list *args)
{
	if (args == NULL) {
		cry(c, "var", CHEAX_EMATCH, "Invalid declaration");
		return NULL;
	}

	struct chx_value *idval = args->value;
	args = args->next;

	struct chx_value *setto = NULL;
	if (args != NULL) {
		setto = cheax_evalp(c, args->value, pad);
		args = args->next;
	}

	if (args != NULL) {
		cry(c, "var", CHEAX_EMATCH, "Invalid declaration");
		return NULL;
	}

	if (!pan_match(c, idval, setto)) {
		cry(c, "var", CHEAX_EMATCH, "Invalid pattern");
		return NULL;
	}

pad:
	return NULL;
}

static struct chx_value *builtin_set(CHEAX *c, struct chx_list *args)
{
	if (args == NULL) {
		cry(c, "set", CHEAX_EMATCH, "Set expects a variable name");
		return NULL;
	}

	struct chx_value *idval = args->value;
	if (cheax_get_type(idval) != CHEAX_ID) {
		cry(c, "set", CHEAX_ETYPE, "Expected an identifier");
		return NULL;
	}
	struct chx_id *id = (struct chx_id *)idval;

	args = args->next;
	if (args == NULL) {
		cry(c, "set", CHEAX_EMATCH, "Set expects a value");
		return NULL;
	}

	struct chx_value *setto = cheax_evalp(c, args->value, pad);

	if (args->next != NULL) {
		cry(c, "set", CHEAX_EMATCH, "Invalid set");
		return NULL;
	}

	struct variable *sym = find_sym(c, id->id);
	if (sym == NULL) {
		cry(c, "set", CHEAX_ENOSYM, "No such symbol \"%s\"", id->id);
		return NULL;
	}

	if (sym->flags & CHEAX_READONLY) {
		cry(c, "set", CHEAX_EREADONLY, "Cannot write to read-only variable");
		return NULL;
	}

	if ((sym->flags & CHEAX_SYNCED) == 0) {
		sym->value.norm = setto;
		return NULL;
	}

	switch (sym->ctype) {
	case CTYPE_INT:
		if (!try_convert_to_int(setto, sym->value.sync_int)) {
			cry(c, "set", CHEAX_ETYPE, "Invalid type");
			return NULL;
		}
		break;

	case CTYPE_FLOAT:
		; double d;
		if (!try_convert_to_double(setto, &d)) {
			cry(c, "set", CHEAX_ETYPE, "Invalid type");
			return NULL;
		}
		*sym->value.sync_float = d;
		break;

	case CTYPE_DOUBLE:
		if (!try_convert_to_double(setto, sym->value.sync_double)) {
			cry(c, "set", CHEAX_ETYPE, "Invalid type");
			return NULL;
		}
		break;

	default:
		cry(c, "set", CHEAX_EEVAL, "Unexpected sync-type");
		return NULL;
	}

pad:
	return NULL;
}

static struct chx_value *builtin_prepend(CHEAX *c, struct chx_list *args)
{
	struct chx_value *head, *tail;
	if (!unpack_args(c, ":", args, 2, &head, &tail))
		return NULL;

	int tailty = cheax_get_type(tail);
	if (tailty != CHEAX_NIL && tailty != CHEAX_LIST) {
		cry(c, ":", CHEAX_ETYPE, "Improper list not allowed");
		return NULL;
	}

	return &cheax_list(head, (struct chx_list *)tail)->base;
}

static struct chx_value *builtin_get_type(CHEAX *c, struct chx_list *args)
{
	struct chx_value *val;
	if (!unpack_args(c, "get-type", args, 1, &val))
		return NULL;

	return &cheax_int(cheax_get_type(val))->base;
}

static struct chx_value *builtin_get_max_stack_depth(CHEAX *c, struct chx_list *args)
{
	if (!unpack_args(c, "get-max-stack-depth", args, 0))
		return NULL;

	return &cheax_int(c->max_stack_depth)->base;
}

static struct chx_value *builtin_set_max_stack_depth(CHEAX *c, struct chx_list *args)
{
	struct chx_value *value;
	if (!unpack_args(c, "set-max-stack-depth", args, 1, &value))
		return NULL;

	if (cheax_get_type(value) != CHEAX_INT) {
		cry(c, "set-max-stack-depth", CHEAX_ETYPE, "Expected integer argument");
		return NULL;
	}

	struct chx_int *int_arg = (struct chx_int *)value;
	int ivalue = int_arg->value;

	cheax_set_max_stack_depth(c, ivalue);
	return NULL;
}

static struct chx_value *create_func(CHEAX *c,
                                     const char *name,
                                     struct chx_list *args,
                                     bool eval_args)
{
	if (args == NULL) {
		cry(c, name, CHEAX_EMATCH, "Invalid lambda");
		return NULL;
	}

	struct chx_value *arg_list = args->value;
	struct chx_list *body = args->next;

	if (body == NULL) {
		cry(c, name, CHEAX_EMATCH, "Invalid lambda");
		return NULL;
	}

	struct chx_func *res = GC_MALLOC(sizeof(struct chx_func));
	res->base.type = CHEAX_FUNC;
	res->eval_args = eval_args;
	res->args = arg_list;
	res->body = body;
	res->locals_top = c->locals_top;
	return &res->base;
}

static struct chx_value *builtin_lambda(CHEAX *c, struct chx_list *args)
{
	return create_func(c, "\\", args, true);
}
static struct chx_value *builtin_macro_lambda(CHEAX *c, struct chx_list *args)
{
	return create_func(c, "\\\\", args, false);
}

static struct chx_value *builtin_eval(CHEAX *c, struct chx_list *args)
{
	struct chx_value *arg;
	if (!unpack_args(c, "eval", args, 1, &arg))
		return NULL;

	return cheax_eval(c, arg);
}

static struct chx_value *builtin_case(CHEAX *c, struct chx_list *args)
{
	if (args == NULL) {
		cry(c, "case", CHEAX_EMATCH, "Invalid case");
		return NULL;
	}

	struct chx_value *what = cheax_evalp(c, args->value, pad);

	/* pattern-value-pair */
	for (struct chx_list *pvp = args->next; pvp; pvp = pvp->next) {
		struct chx_value *pair = pvp->value;
		if (cheax_get_type(pair) != CHEAX_LIST) {
			cry(c, "case", CHEAX_EMATCH, "Pattern-value pair expected");
			return NULL;
		}
		struct chx_list *cons_pair = (struct chx_list *)pair;
		struct chx_value *pan = cons_pair->value;
		struct variable *top_before = c->locals_top;
		if (!pan_match(c, pan, what)) {
			c->locals_top = top_before;
			continue;
		}

		/* pattern matches! */
		struct chx_value *retval = NULL;
		for (struct chx_list *val = cons_pair->next; val; val = val->next)
			retval = cheax_evalp(c, val->value, pad);
		c->locals_top = top_before;
		return retval;
	}

	cry(c, "case", CHEAX_EMATCH, "Non-exhaustive pattern");

pad:
	return NULL;
}


static bool is_numeric_type(struct chx_value *val)
{
	int ty = cheax_get_type(val);
	return (ty == CHEAX_INT) || (ty == CHEAX_DOUBLE);
}

static struct chx_value *do_aop(CHEAX *c,
                                const char *name,
                                struct chx_list *args,
                                int    (*iop)(CHEAX *, int   , int   ),
                                double (*fop)(CHEAX *, double, double))
{
	struct chx_value *l, *r;
	if (!unpack_args(c, name, args, 2, &l, &r))
		return NULL;

	if (!is_numeric_type(l) || !is_numeric_type(r)) {
		cry(c, name, CHEAX_ETYPE, "Invalid types for operation");
		return NULL;
	}

	if (cheax_get_type(l) == CHEAX_INT && cheax_get_type(r) == CHEAX_INT) {
		int li = ((struct chx_int *)l)->value;
		int ri = ((struct chx_int *)r)->value;
		int res = iop(c, li, ri);
		if (c->error == CHEAX_EDIVZERO)
			return NULL;

		return &cheax_int(iop(c, li, ri))->base;
	}

	if (fop == NULL) {
		cry(c, name, CHEAX_ETYPE, "Invalid operation on floating point numbers");
		return NULL;
	}

	double ld, rd;
	try_convert_to_double(l, &ld);
	try_convert_to_double(r, &rd);
	return &cheax_double(fop(c, ld, rd))->base;
}

static int    iop_add(CHEAX *c, int    a, int    b) { return a + b; }
static double fop_add(CHEAX *c, double a, double b) { return a + b; }
static int    iop_sub(CHEAX *c, int    a, int    b) { return a - b; }
static double fop_sub(CHEAX *c, double a, double b) { return a - b; }
static int    iop_mul(CHEAX *c, int    a, int    b) { return a * b; }
static double fop_mul(CHEAX *c, double a, double b) { return a * b; }
static int    iop_div(CHEAX *c, int    a, int    b)
{
	if (b == 0) {
		cry(c, "/", CHEAX_EDIVZERO, "Division by zero");
		return 0;
	}

	return a / b;
}
static double fop_div(CHEAX *c, double a, double b) { return a / b; }
static int    iop_mod(CHEAX *c, int    a, int    b)
{
	if (b == 0) {
		cry(c, "%", CHEAX_EDIVZERO, "Division by zero");
		return 0;
	}

	return a / b;
}

static struct chx_value *builtin_add(CHEAX *c, struct chx_list *args)
{
	return do_aop(c, "+", args, iop_add, fop_add);
}
static struct chx_value *builtin_sub(CHEAX *c, struct chx_list *args)
{
	return do_aop(c, "-", args, iop_sub, fop_sub);
}
static struct chx_value *builtin_mul(CHEAX *c, struct chx_list *args)
{
	return do_aop(c, "*", args, iop_mul, fop_mul);
}
static struct chx_value *builtin_div(CHEAX *c, struct chx_list *args)
{
	return do_aop(c, "/", args, iop_div, fop_div);
}
static struct chx_value *builtin_mod(CHEAX *c, struct chx_list *args)
{
	return do_aop(c, "%", args, iop_mod, NULL);
}

/* Note: never calls cheax_eval() */
static bool equals(struct chx_value *l, struct chx_value *r)
{
	if (cheax_get_type(l) != cheax_get_type(r))
		return false;

	int ty = cheax_get_type(l);
	if (cheax_is_user_type(ty))
		return ((struct chx_ptr *)l)->ptr == ((struct chx_ptr *)r)->ptr;

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
		return equals(llist->value, rlist->value) && equals(&llist->next->base, &rlist->next->base);
	case CHEAX_EXT_FUNC:
		return ((struct chx_ext_func *)l)->perform == ((struct chx_ext_func *)r)->perform;
	case CHEAX_QUOTE:
		return equals(((struct chx_quote *)l)->value, ((struct chx_quote *)r)->value);
	case CHEAX_STRING:
		;
		struct chx_string *lstring = (struct chx_string *)l;
		struct chx_string *rstring = (struct chx_string *)r;
		return (lstring->len == rstring->len) && !strcmp(lstring->value, rstring->value);

	default:
		return l == r;
	}
}

static struct chx_value *builtin_eq(CHEAX *c, struct chx_list *args)
{
	struct chx_value *l, *r;
	if (!unpack_args(c, "=", args, 2, &l, &r))
		return NULL;

	return equals(l, r) ? &yes.base : &no.base;
}
static struct chx_value *builtin_lt(CHEAX *c, struct chx_list *args)
{
	struct chx_value *l, *r;
	if (!unpack_args(c, "<", args, 2, &l, &r))
		return NULL;

	if (!is_numeric_type(l) || !is_numeric_type(r)) {
		cry(c, "<", CHEAX_ETYPE, "Invalid types for operation");
		return NULL;
	}

	if (cheax_get_type(l) == CHEAX_INT && cheax_get_type(r) == CHEAX_INT) {
		int li = ((struct chx_int *)l)->value;
		int ri = ((struct chx_int *)r)->value;
		return li < ri ? &yes.base : &no.base;
	}

	double ld, rd;
	try_convert_to_double(l, &ld);
	try_convert_to_double(r, &rd);
	return ld < rd ? &yes.base : &no.base;
}
