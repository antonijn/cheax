/* Copyright (c) 2016, Antonie Blom
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
#include "read.h"

#include <gc.h>
#include <cheax.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DECL_BUILTIN(cname) \
static struct chx_value *builtin_##cname(CHEAX *c, struct chx_cons *args)

DECL_BUILTIN(defmacro);
DECL_BUILTIN(fdopen);
DECL_BUILTIN(fdclose);
DECL_BUILTIN(read_from);
DECL_BUILTIN(print_to);
DECL_BUILTIN(getc);
DECL_BUILTIN(putc);
DECL_BUILTIN(var);
DECL_BUILTIN(const);
DECL_BUILTIN(set);
DECL_BUILTIN(prepend);
DECL_BUILTIN(is_id);
DECL_BUILTIN(is_int);
DECL_BUILTIN(is_double);
DECL_BUILTIN(is_list);
DECL_BUILTIN(lambda);
DECL_BUILTIN(macro_lambda);
DECL_BUILTIN(eval);
DECL_BUILTIN(case);
DECL_BUILTIN(add);
DECL_BUILTIN(sub);
DECL_BUILTIN(mul);
DECL_BUILTIN(div);
DECL_BUILTIN(eq);

#define DEF_BUILTIN(name, cname) \
	static struct chx_macro mc_##cname = { { VK_BUILTIN }, builtin_##cname }; \
	chx_def_sym(c, name, &mc_##cname.base)
void export_builtins(CHEAX *c)
{
	DEF_BUILTIN("fdopen", fdopen);
	DEF_BUILTIN("fdclose", fdclose);
	DEF_BUILTIN("read-from", read_from);
	DEF_BUILTIN("print-to", print_to);
	DEF_BUILTIN("var", var);
	DEF_BUILTIN("const", const);
	DEF_BUILTIN("set", set);
	DEF_BUILTIN(":", prepend);
	DEF_BUILTIN("is-id", is_id);
	DEF_BUILTIN("is-int", is_int);
	DEF_BUILTIN("is-double", is_double);
	DEF_BUILTIN("is-list", is_list);
	DEF_BUILTIN("\\", lambda);
	DEF_BUILTIN("\\\\", macro_lambda);
	DEF_BUILTIN("eval", eval);
	DEF_BUILTIN("case", case);
	DEF_BUILTIN("+", add);
	DEF_BUILTIN("-", sub);
	DEF_BUILTIN("*", mul);
	DEF_BUILTIN("/", div);
	DEF_BUILTIN("=", eq);
}

static bool expect_args(CHEAX *c, const char *fname, int num, struct chx_cons *args)
{
	if (num == 0)
		return true;
	if (!args) {
		cry(c, fname, "Expected %d argument%s", num, num != 1 ? "s" : "");
		return false;
	}
	return expect_args(c, fname, num - 1, args->next);
}
#define EXPECT_ARGS(c, fname, num, args) \
	if (!expect_args(c, fname, num, args)) \
		return NULL

static double to_double(struct chx_value *x)
{
	if (x->kind == VK_INT)
		return ((struct chx_int *)x)->value;
	return ((struct chx_double *)x)->value;
}
static int to_int(struct chx_value *x)
{
	if (x->kind == VK_INT)
		return ((struct chx_int *)x)->value;
	return ((struct chx_double *)x)->value;
}

static struct chx_value *builtin_fdopen(CHEAX *c, struct chx_cons *args)
{
	EXPECT_ARGS(c, "fdopen", 1, args);
	return NULL;
}
static struct chx_value *builtin_fdclose(CHEAX *c, struct chx_cons *args)
{
	EXPECT_ARGS(c, "fdclose", 1, args);
	return NULL;
}
static struct chx_value *builtin_read_from(CHEAX *c, struct chx_cons *args)
{
	EXPECT_ARGS(c, "read-from", 1, args);
	int fd = to_int(cheax_eval(c, args->value));
	FILE *fp = fdopen(fd, "rb");
	return cheax_read(fp);
}
static struct chx_value *builtin_print_to(CHEAX *c, struct chx_cons *args)
{
	EXPECT_ARGS(c, "print-to", 2, args);
	int fd = to_int(cheax_eval(c, args->value));
	FILE *fp = fdopen(fd, "wb");
	cheax_print(fp, cheax_eval(c, args->next->value));
	fprintf(fp, "\n");
	return NULL;
}

static struct chx_value *builtin_const(CHEAX *c, struct chx_cons *args)
{
	EXPECT_ARGS(c, "const", 1, args);

	struct chx_value *idval = args->value;
	struct chx_value *setto = args->next ? cheax_eval(c, args->next->value) : NULL;
	struct variable *prev_top = c->locals_top;
	if (!pan_match(c, idval, setto)) {
		cry(c, "const", "Invalid pattern");
		return NULL;
	}
	if (setto && setto->kind == VK_LAMBDA) {
		/* to allow for recursion:
 		 * if this wasn't here, the function symbol would not have been
 		 * available to the lambda, and thus a recursive function would
 		 * give an undefined symbol error.
 		 */
		((struct chx_lambda *)setto)->locals_top = c->locals_top;
	}
	for (struct variable *v = c->locals_top; v != prev_top; v = v->below)
		v->flags |= SF_RO;
	return NULL;
}

static struct chx_value *builtin_var(CHEAX *c, struct chx_cons *args)
{
	EXPECT_ARGS(c, "var", 1, args);

	struct chx_value *idval = args->value;
	struct chx_value *setto = args->next ? cheax_eval(c, args->next->value) : NULL;
	if (!pan_match(c, idval, setto)) {
		cry(c, "var", "Invalid pattern");
		return NULL;
	}
	return NULL;
}

static struct chx_value *builtin_set(CHEAX *c, struct chx_cons *args)
{
	EXPECT_ARGS(c, "set", 2, args);

	struct chx_value *idval = args->value;
	struct chx_value *setto = cheax_eval(c, args->next->value);

	struct chx_id *id = (struct chx_id *)idval;
	struct variable *sym = chx_find_sym(c, id->id);
	if (sym->flags & SF_RO) {
		cry(c, "set", "Cannot write to constant");
		return NULL;
	}
	if (sym->flags & SF_SYNCED) {
		switch (sym->sync_var.ty) {
		case CHEAX_INT:
			*(int *)sym->sync_var.var = to_int(setto);
			break;
		case CHEAX_BOOL:
			*(bool *)sym->sync_var.var = to_int(setto);
			break;
		case CHEAX_FLOAT:
			*(float *)sym->sync_var.var = to_double(setto);
			break;
		case CHEAX_DOUBLE:
			*(double *)sym->sync_var.var = to_double(setto);
			break;
		}
	} else {
		sym->value = setto;
	}
	return NULL;
}

static struct chx_value *builtin_prepend(CHEAX *c, struct chx_cons *args)
{
	EXPECT_ARGS(c, ":", 2, args);

	struct chx_cons *car = args;
	struct chx_cons *cdr = car->next;
	struct chx_value *carv = cheax_eval(c, car->value);
	struct chx_value *cdrv = cheax_eval(c, cdr->value);
	if (cdrv != NULL && cdrv->kind != VK_CONS) {
		cry(c, ":", "Improper list no.baset allowed");
		return NULL;
	}

	return &cheax_cons(carv, (struct chx_cons *)cdrv)->base;
}

static struct chx_int yes = { { VK_INT }, 1 }, no = { { VK_INT }, 0 };

static struct chx_value *builtin_is_int(CHEAX *c, struct chx_cons *args)
{
	EXPECT_ARGS(c, "is-int", 1, args);
	struct chx_value *val = cheax_eval(c, args->value);
	return val && val->kind == VK_INT ? &yes.base : &no.base;
}

static struct chx_value *builtin_is_double(CHEAX *c, struct chx_cons *args)
{
	EXPECT_ARGS(c, "is-double", 1, args);
	struct chx_value *val = cheax_eval(c, args->value);
	return val && val->kind == VK_DOUBLE ? &yes.base : &no.base;
}

static struct chx_value *builtin_is_id(CHEAX *c, struct chx_cons *args)
{
	EXPECT_ARGS(c, "is-id", 1, args);
	struct chx_value *val = cheax_eval(c, args->value);
	return val && val->kind == VK_ID ? &yes.base : &no.base;
}

static struct chx_value *builtin_is_list(CHEAX *c, struct chx_cons *args)
{
	EXPECT_ARGS(c, "is-list", 1, args);
	struct chx_value *val = cheax_eval(c, args->value);
	return val && val->kind == VK_CONS ? &yes.base : &no.base;
}

static struct chx_value *create_lambda(CHEAX *c, struct chx_cons *args, bool eval_args)
{
	struct chx_value *arg_list = args->value;
	struct chx_cons *body = args->next;

	struct chx_lambda *res = GC_MALLOC(sizeof(struct chx_lambda));
	res->base.kind = VK_LAMBDA;
	res->eval_args = eval_args;
	res->args = arg_list;
	res->body = body;
	res->locals_top = c->locals_top;
	return &res->base;
}

static struct chx_value *builtin_lambda(CHEAX *c, struct chx_cons *args)
{
	EXPECT_ARGS(c, "\\", 2, args);
	return create_lambda(c, args, true);
}
static struct chx_value *builtin_macro_lambda(CHEAX *c, struct chx_cons *args)
{
	EXPECT_ARGS(c, "\\\\", 2, args);
	return create_lambda(c, args, false);
}

static struct chx_value *builtin_eval(CHEAX *c, struct chx_cons *args)
{
	EXPECT_ARGS(c, "eval", 1, args);
	return cheax_eval(c, cheax_eval(c, args->value));
}

static struct chx_value *builtin_case(CHEAX *c, struct chx_cons *args)
{
	EXPECT_ARGS(c, "case", 1, args);

	struct chx_value *what = cheax_eval(c, args->value);
	/* pattern-value-pair */
	for (struct chx_cons *pvp = args->next; pvp; pvp = pvp->next) {
		struct chx_value *pair = pvp->value;
		if (!pair || pair->kind != VK_CONS) {
			cry(c, "case", "Pattern-value pair expected");
			return NULL;
		}
		struct chx_cons *cons_pair = (struct chx_cons *)pair;
		struct chx_value *pan = cons_pair->value;
		struct variable *top_before = c->locals_top;
		if (!pan_match(c, pan, what)) {
			c->locals_top = top_before;
			continue;
		}
		/* pattern matches! */
		struct chx_value *retval = NULL;
		for (struct chx_cons *val = cons_pair->next; val; val = val->next)
			retval = cheax_eval(c, val->value);
		c->locals_top = top_before;
		return retval;
	}
	cry(c, "case", "Non-exhaustive pattern");
	return NULL;
}

static bool aop_use_double(struct chx_value *l, struct chx_value *r)
{
	return l && r && (l->kind == VK_DOUBLE || r->kind == VK_DOUBLE);
}
#define DO_AOP(c, l, r, op) \
	if (aop_use_double(l, r)) { \
		double res = to_double(l) op to_double(r); \
		struct chx_double *full_res = GC_MALLOC(sizeof(struct chx_double)); \
		*full_res = (struct chx_double){ { VK_DOUBLE }, res }; \
		return &full_res->base; \
	} else { \
		int res = ((struct chx_int *)l)->value op ((struct chx_int *)r)->value; \
		struct chx_int *full_res = GC_MALLOC(sizeof(struct chx_int)); \
		*full_res = (struct chx_int){ { VK_INT }, res }; \
		return &full_res->base; \
	}

static struct chx_value *builtin_add(CHEAX *c, struct chx_cons *args)
{
	EXPECT_ARGS(c, "add", 2, args);
	struct chx_value *l = cheax_eval(c, args->value);
	struct chx_value *r = cheax_eval(c, args->next->value);
	DO_AOP(c, l, r, +);
}
static struct chx_value *builtin_sub(CHEAX *c, struct chx_cons *args)
{
	EXPECT_ARGS(c, "sub", 2, args);
	struct chx_value *l = cheax_eval(c, args->value);
	struct chx_value *r = cheax_eval(c, args->next->value);
	DO_AOP(c, l, r, -);
}
static struct chx_value *builtin_mul(CHEAX *c, struct chx_cons *args)
{
	EXPECT_ARGS(c, "mul", 2, args);
	struct chx_value *l = cheax_eval(c, args->value);
	struct chx_value *r = cheax_eval(c, args->next->value);
	DO_AOP(c, l, r, *);
}
static struct chx_value *builtin_div(CHEAX *c, struct chx_cons *args)
{
	EXPECT_ARGS(c, "div", 2, args);
	struct chx_value *l = cheax_eval(c, args->value);
	struct chx_value *r = cheax_eval(c, args->next->value);
	DO_AOP(c, l, r, /);
}
static struct chx_value *builtin_eq(CHEAX *c, struct chx_cons *args)
{
	EXPECT_ARGS(c, "eq", 2, args);
	struct chx_value *l = cheax_eval(c, args->value);
	struct chx_value *r = cheax_eval(c, args->next->value);
	if (pan_match(c, l, r)) /* this can go horribly wrong TODO */
		return &yes.base;
	else
		return &no.base;
}
