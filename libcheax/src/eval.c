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

#include <cheax.h>
#include "eval.h"
#include "builtins.h"

#include <gc.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static struct variable *def_sym(CHEAX *c, const char *name, struct chx_value *val);

struct chx_cons *cheax_cons(struct chx_value *car, struct chx_cons *cdr)
{
	struct chx_cons *res = GC_MALLOC(sizeof(struct chx_cons));
	res->base.kind = VK_CONS;
	res->value = car;
	res->next = cdr;
	return res;
}

static bool pan_match_cheax_cons(CHEAX *c, struct chx_cons *pan, struct chx_cons *match);

bool pan_match(CHEAX *c, struct chx_value *pan, struct chx_value *match)
{
	if (pan == NULL)
		return match == NULL;
	if (pan->kind == VK_ID) {
		def_sym(c, ((struct chx_id *)pan)->id, match);
		return true;
	}
	if (match == NULL)
		return false;
	if (pan->kind == VK_INT) {
		if (match->kind != VK_INT)
			return false;
		return ((struct chx_int *)pan)->value == ((struct chx_int *)match)->value;
	}
	if (pan->kind == VK_DOUBLE) {
		if (match->kind != VK_DOUBLE)
			return false;
		return ((struct chx_double *)pan)->value == ((struct chx_double *)match)->value;
	}
	if (pan->kind == VK_CONS) {
		struct chx_cons *pan_cons = (struct chx_cons *)pan;
		if (match->kind != VK_CONS)
			return false;
		struct chx_cons *match_cons = (struct chx_cons *)match;

		return pan_match_cheax_cons(c, pan_cons, match_cons);
	}
	return false;
}

static bool pan_match_colon_cheax_cons(CHEAX *c, struct chx_cons *pan, struct chx_cons *match)
{
	if (!pan->next)
		return pan_match(c, pan->value, &match->base);
	if (!match)
		return false;
	if (!pan_match(c, pan->value, match->value))
		return false;
	return pan_match_colon_cheax_cons(c, pan->next, match->next);
}

static bool pan_match_cheax_cons(CHEAX *c, struct chx_cons *pan, struct chx_cons *match)
{
	if (pan->value
	 && pan->value->kind == VK_ID
	 && !strcmp((((struct chx_id *)pan->value)->id), ":"))
	{
		return pan_match_colon_cheax_cons(c, pan->next, match);
	}

	while (pan && match) {
		if (!pan_match(c, pan->value, match->value))
			return false;

		pan = pan->next;
		match = match->next;
	}

	return (pan == NULL) && (match == NULL);
}

static unsigned djb2(const char *str)
{
	unsigned hash = 5381;
	int c;
	while (c = *str++)
		hash = ((hash << 5) + hash) + c;
	return hash;
}

struct variable *find_sym(CHEAX *c, const char *name)
{
	unsigned hash = djb2(name);
	for (struct variable *ht = c->locals_top; ht; ht = ht->below)
		if (ht->hash == hash && !strcmp(name, ht->name))
			return ht;
	return NULL;
}

static struct variable *def_sym(CHEAX *c, const char *name, struct chx_value *val)
{
	struct variable *new = GC_MALLOC(sizeof(struct variable));
	new->flags = SF_DEFAULT;
	new->value = val;
	new->hash = djb2(name);
	new->name = name;
	new->below = c->locals_top;
	c->locals_top = new;
	return new;
}

/*
 * Synchronizes variable with given flags.
 */
static void sync(CHEAX *c, const char *name, enum cheax_type ty, void *var, enum varflags flags);

CHEAX *cheax_init(void)
{
	CHEAX *res = malloc(sizeof(struct cheax));
	res->locals_top = NULL;
	res->max_stack_depth = 0x1000;
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
		cry(c, "cheax_set_max_stack_depth", "Maximum stack depth must be positive");
}

static void sync(CHEAX *c, const char *name, enum cheax_type ty, void *var, enum varflags flags)
{
	struct variable *new = def_sym(c, name, NULL);
	new->flags |= SF_SYNCED | flags;
	new->sync_var.var = var;
	new->sync_var.ty = ty;
}

void cheax_sync(CHEAX *c, const char *name, enum cheax_type ty, void *var)
{
	sync(c, name, ty, var, SF_DEFAULT);
}
void cheax_syncro(CHEAX *c, const char *name, enum cheax_type ty, const void *var)
{
	sync(c, name, ty, (void *)var, SF_RO | SF_NODUMP);
}
void cheax_syncnd(CHEAX *c, const char *name, enum cheax_type ty, void *var)
{
	sync(c, name, ty, var, SF_NODUMP);
}

void cheax_defmacro(CHEAX *c, const char *name, macro fun)
{
	struct chx_macro *mc = GC_MALLOC(sizeof(struct chx_macro));
	*mc = (struct chx_macro){
		.base = { VK_BUILTIN },
		.perform = fun
	};
	def_sym(c, name, &mc->base);
}

int cheax_load_prelude(CHEAX *c)
{
	FILE *f = fopen(CMAKE_INSTALL_PREFIX "/share/cheax/prelude.chx", "rb");
	if (!f)
		return -1;
	cheax_exec(c, f);
	fclose(f);
	return 0;
}

void cheax_exec(CHEAX *c, FILE *f)
{
	char shebang[2] = { 0 };
	fread(shebang, 2, 1, f);
	if (shebang[0] == '#' && shebang[1] == '!') {
		while (fgetc(f) != '\n')
			;
	} else {
		ungetc(shebang[1], f);
		ungetc(shebang[0], f);
	}

	struct chx_value *v;
	while (v = cheax_read(f))
		cheax_eval(c, v);
}

static struct chx_value *cheax_safe_eval(CHEAX *c, struct chx_value *input, int stack_depth);
static struct chx_value *cheax_eval_sexpr(CHEAX *c, struct chx_cons *input, int stack_depth);

struct chx_value *cheax_eval(CHEAX *c, struct chx_value *input)
{
	cheax_safe_eval(c, input, 0);
}

static struct chx_value *cheax_safe_eval(CHEAX *c, struct chx_value *input, int stack_depth)
{
	if (!input)
		return NULL;

	switch (input->kind) {
	case VK_INT:
	case VK_DOUBLE:
	case VK_LAMBDA:
	case VK_BUILTIN:
	case VK_PTR:
		return input;
	case VK_ID:
		; struct variable *sym = find_sym(c, ((struct chx_id *)input)->id);
		if (!sym) {
			cry(c, "eval", "No such symbol '%s'", ((struct chx_id *)input)->id);
			return NULL;
		}
		if ((sym->flags & SF_SYNCED) == 0)
			return sym->value;
		switch (sym->sync_var.ty) {
		case CHEAX_INT:
			; struct chx_int *int_res = GC_MALLOC(sizeof(struct chx_int));
			*int_res = (struct chx_int){ { VK_INT }, *(int *)sym->sync_var.var };
			return &int_res->base;
		case CHEAX_DOUBLE:
			; struct chx_double *double_res = GC_MALLOC(sizeof(struct chx_double));
			*double_res = (struct chx_double){ { VK_DOUBLE }, *(double *)sym->sync_var.var };
			return &double_res->base;
		case CHEAX_FLOAT:
			; struct chx_double *float_res = GC_MALLOC(sizeof(struct chx_double));
			*float_res = (struct chx_double){ { VK_DOUBLE }, *(float *)sym->sync_var.var };
			return &float_res->base;
		case CHEAX_BOOL:
			; struct chx_int *bool_res = GC_MALLOC(sizeof(struct chx_int));
			*bool_res = (struct chx_int){ { VK_INT }, *(bool *)sym->sync_var.var };
			return &bool_res->base;
		case CHEAX_PTR:
			; struct chx_ptr *ptr_res = GC_MALLOC(sizeof(struct chx_ptr));
			*ptr_res = (struct chx_ptr){ { VK_PTR }, *(void **)sym->sync_var.var };
			return &ptr_res->base;
		}
	case VK_CONS:
		if (stack_depth >= c->max_stack_depth) {
			cry(c, "eval", "Stack overflow! (maximum stack depth = %d)", c->max_stack_depth);
			return NULL;
		}

		return cheax_eval_sexpr(c, (struct chx_cons *)input, stack_depth + 1);
	case VK_QUOTE:
		return ((struct chx_quote *)input)->value;
	}
	return NULL;
}

static struct chx_value *expand_macro(struct variable *args_top,
                                      struct variable *locals_top,
                                      struct chx_value *val,
                                      int stack_depth)
{
	if (!val)
		return NULL;

	struct chx_id *as_id = (struct chx_id *)val;
	struct chx_cons *as_cons = (struct chx_cons *)val;
	struct chx_quote *as_quote = (struct chx_quote *)val;
	switch (val->kind) {
	case VK_ID:
		; unsigned hash = djb2(as_id->id);
		for (struct variable *v = args_top; v != locals_top; v = v->below)
			if (v->hash == hash && !strcmp(v->name, as_id->id))
				return v->value; /* args cannot be synced afaik */
		return val;
	case VK_CONS:
		; struct chx_cons *cons_res = NULL;
		struct chx_cons **cons_last = &cons_res;
		for (; as_cons; as_cons = as_cons->next) {
			*cons_last = cheax_cons(expand_macro(args_top, locals_top, as_cons->value, stack_depth), NULL);
			cons_last = &(*cons_last)->next;
		}
		return &cons_res->base;
	case VK_QUOTE:
		; struct chx_quote *quotres = GC_MALLOC(sizeof(struct chx_quote));
		quotres->base.kind = VK_QUOTE;
		quotres->value = expand_macro(args_top, locals_top, as_quote->value, stack_depth);
		return &quotres->base;
	default:
		return val;
	}
}
static struct chx_value *call_macro(CHEAX *c,
                                    struct chx_lambda *lda,
                                    struct chx_cons *args,
                                    int stack_depth)
{
	struct variable *prev_top = c->locals_top;
	pan_match(c, lda->args, &args->base);
	struct variable *new_top = c->locals_top;
	c->locals_top = prev_top;

	struct chx_value *retval = NULL;
	for (struct chx_cons *cons = lda->body; cons; cons = cons->next)
		retval = cheax_safe_eval(c, expand_macro(new_top, prev_top, cons->value, stack_depth), stack_depth);

	return retval;
}
static struct chx_value *call_func(CHEAX *c,
                                   struct chx_lambda *lda,
                                   struct chx_cons *args,
                                   int stack_depth)
{
	pan_match(c, lda->args, &args->base);
	struct chx_value *retval = NULL;
	for (struct chx_cons *cons = lda->body; cons; cons = cons->next)
		retval = cheax_safe_eval(c, cons->value, stack_depth);
	return retval;
}

static struct chx_value *cheax_eval_sexpr(CHEAX *c, struct chx_cons *input, int stack_depth)
{
	struct chx_value *func = cheax_safe_eval(c, input->value, stack_depth);
	if (func == NULL)
		return NULL;
	if (func->kind == VK_BUILTIN) {
		struct chx_macro *macro = (struct chx_macro *)func;
		return macro->perform(c, input->next);
	}
	if (func->kind == VK_LAMBDA) {
		struct chx_lambda *lda = (struct chx_lambda *)func;
		if (!lda->eval_args) {
			/* macro call */
			struct chx_cons *args = input->next;
			return call_macro(c, lda, args, stack_depth);
		}

		/* function call */

		/* evaluate arguments */
		struct chx_cons *args = NULL;
		struct chx_cons **args_last = &args;
		for (struct chx_cons *arg = input->next; arg; arg = arg->next) {
			*args_last = cheax_cons(cheax_safe_eval(c, arg->value, stack_depth), NULL);
			args_last = &(*args_last)->next;
		}

		/* create new context for function to run in */
		struct variable *prev_locals_top = c->locals_top;
		c->locals_top = lda->locals_top;

		struct chx_value *retval = call_func(c, lda, args, stack_depth);
		/* restore previous context */
		c->locals_top = prev_locals_top;
		return retval;
	}

	cry(c, "eval", "Invalid function call");
	return NULL;
}

void cheax_print(FILE *c, struct chx_value *first)
{
	if (!first) {
		fprintf(c, "()");
		return;
	}

	if (first->kind == VK_INT) {
		fprintf(c, "%d", ((struct chx_int *)first)->value);
	} else if (first->kind == VK_DOUBLE) {
		fprintf(c, "%f", ((struct chx_double *)first)->value);
	} else if (first->kind == VK_ID) {
		fprintf(c, "%s", ((struct chx_id *)first)->id);
	} else if (first->kind == VK_CONS) {
		struct chx_cons *cons = (struct chx_cons *)first;
		fprintf(c, "(");
		for (; cons; cons = cons->next) {
			cheax_print(c, cons->value);
			if (cons->next)
				fprintf(c, " ");
		}
		fprintf(c, ")");
	} else if (first->kind == VK_QUOTE) {
		fprintf(c, "'");
		cheax_print(c, ((struct chx_quote *)first)->value);
	} else if (first->kind == VK_LAMBDA) {
		fprintf(c, "(");
		struct chx_lambda *lambda = (struct chx_lambda *)first;
		if (lambda->eval_args)
			fprintf(c, "\\ ");
		else
			fprintf(c, "\\\\ ");
		cheax_print(c, lambda->args);
		struct chx_cons *body = lambda->body;
		for (; body; body = body->next) {
			fprintf(c, "\n  ");
			cheax_print(c, body->value);
		}
		fprintf(c, ")");
	} else if (first->kind == VK_PTR) {
		fprintf(c, "%p", ((struct chx_ptr *)first)->ptr);
	}
}
