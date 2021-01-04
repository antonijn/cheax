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

#include <cheax.h>
#include "api.h"
#include "builtins.h"

#include <gc.h>
#include <string.h>

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
	while (v = cheax_read(c, f)) {
		cheax_eval(c, v);
		cheax_ft(c, pad);
	}

pad:
	return;
}

static struct chx_value *cheax_eval_sexpr(CHEAX *c, struct chx_list *input);

struct chx_value *cheax_eval(CHEAX *c, struct chx_value *input)
{
	switch (cheax_get_type(input)) {
	case CHEAX_ID:
		return cheax_get(c, ((struct chx_id *)input)->id);
	case CHEAX_LIST:
		if (c->stack_depth >= c->max_stack_depth) {
			cry(c, "eval", CHEAX_ESTACK, "Stack overflow! (maximum stack depth = %d)", c->max_stack_depth);
			return NULL;
		}

		int prev_stack_depth = c->stack_depth++;
		struct chx_value *res = cheax_eval_sexpr(c, (struct chx_list *)input);
		c->stack_depth = prev_stack_depth;

		return res;
	case CHEAX_QUOTE:
		return ((struct chx_quote *)input)->value;

	default:
		return input;
	}
}

static struct chx_value *expand_macro(CHEAX *c,
                                      struct variable *args_top,
                                      struct variable *locals_top,
                                      struct chx_value *val)
{
	struct chx_id *as_id = (struct chx_id *)val;
	struct chx_list *as_cons = (struct chx_list *)val;
	struct chx_quote *as_quote = (struct chx_quote *)val;

	switch (cheax_get_type(val)) {
	case CHEAX_ID:
		for (struct variable *v = args_top; v != locals_top; v = v->below)
			if (!strcmp(v->name, as_id->id))
				return v->value.norm; /* args cannot be synced afaik */
		return val;
	case CHEAX_LIST:
		; struct chx_list *cons_res = NULL;
		struct chx_list **cons_last = &cons_res;
		for (; as_cons; as_cons = as_cons->next) {
			*cons_last = cheax_list(c, expand_macro(c, args_top, locals_top, as_cons->value), NULL);
			cons_last = &(*cons_last)->next;
		}
		return &cons_res->base;
	case CHEAX_QUOTE:
		; struct chx_quote *quotres = GC_MALLOC(sizeof(struct chx_quote));
		quotres->base.type = CHEAX_QUOTE;
		quotres->value = expand_macro(c, args_top, locals_top, as_quote->value);
		return &quotres->base;
	default:
		return val;
	}
}
static struct chx_value *call_macro(CHEAX *c, struct chx_func *lda, struct chx_list *args)
{
	struct variable *prev_top = c->locals_top;
	if (!cheax_match(c, lda->args, &args->base)) {
		cry(c, "eval", CHEAX_EMATCH, "Invalid (number of) arguments");
		return NULL;
	}
	struct variable *new_top = c->locals_top;
	c->locals_top = prev_top;

	struct chx_value *retval = NULL;
	for (struct chx_list *cons = lda->body; cons; cons = cons->next) {
		retval = cheax_eval(c, expand_macro(c, new_top, prev_top, cons->value));
		cheax_ft(c, pad);
	}

	return retval;

pad:
	return NULL;
}
static struct chx_value *call_func(CHEAX *c, struct chx_func *lda, struct chx_list *args)
{
	if (!cheax_match(c, lda->args, &args->base)) {
		cry(c, "eval", CHEAX_EMATCH, "Invalid (number of) arguments");
		return NULL;
	}

	struct chx_value *retval = NULL;
	for (struct chx_list *cons = lda->body; cons; cons = cons->next) {
		retval = cheax_eval(c, cons->value);
		cheax_ft(c, pad);
	}
	return retval;

pad:
	return NULL;
}

static struct chx_value *cheax_eval_sexpr(CHEAX *c, struct chx_list *input)
{
	struct chx_value *func = cheax_eval(c, input->value);
	cheax_ft(c, pad);
	switch (cheax_get_type(func)) {
	case CHEAX_NIL:
		cry(c, "eval", CHEAX_ENIL, "Cannot call nil");
		return NULL;
	case CHEAX_EXT_FUNC:
		; struct chx_ext_func *extf = (struct chx_ext_func *)func;
		return extf->perform(c, input->next);
	case CHEAX_FUNC:
		; struct chx_func *lda = (struct chx_func *)func;
		if (!lda->eval_args) {
			/* macro call */
			struct chx_list *args = input->next;
			return call_macro(c, lda, args);
		}

		/* function call */

		/* evaluate arguments */
		struct chx_list *args = NULL;
		struct chx_list **args_last = &args;
		for (struct chx_list *arg = input->next; arg; arg = arg->next) {
			struct chx_value *arge = cheax_eval(c, arg->value);
			cheax_ft(c, pad);
			*args_last = cheax_list(c, arge, NULL);
			args_last = &(*args_last)->next;
		}

		/* create new context for function to run in */
		struct variable *prev_locals_top = c->locals_top;
		c->locals_top = lda->locals_top;

		struct chx_value *retval = call_func(c, lda, args);
		/* restore previous context */
		c->locals_top = prev_locals_top;
		return retval;
	case CHEAX_TYPECODE:
		; int type = ((struct chx_int *)func)->value;

		if (!cheax_is_valid_type(c, type)) {
			cry(c, "eval", CHEAX_ETYPE, "Invalid typecode %d", type);
			return NULL;
		}

		struct chx_list *cast_args = input->next;
		if (cast_args == NULL || cast_args->next != NULL) {
			cry(c, "eval", CHEAX_EMATCH, "Expected single argument to cast");
			return NULL;
		}

		struct chx_value *cast_arg = cast_args->value;

		if (cheax_get_base_type(c, type) != cheax_get_type(cast_arg)) {
			cry(c, "eval", CHEAX_ETYPE, "Unable to instantiate");
			return NULL;
		}

		return cheax_cast(c, cast_arg, type);
	}

	cry(c, "eval", CHEAX_ETYPE, "Invalid function call");

pad:
	return NULL;
}

