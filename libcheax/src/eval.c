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

#include "api.h"
#include "gc.h"

void
cheax_exec(CHEAX *c, FILE *f)
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
	while ((v = cheax_read(c, f)) != NULL) {
		cheax_eval(c, v);
		cheax_ft(c, pad);
	}

pad:
	return;
}

static struct chx_list *
eval_args(CHEAX *c, struct chx_list *input)
{
	struct chx_list *args = NULL;
	struct chx_list **args_last = &args;
	for (struct chx_list *arg = input; arg != NULL; arg = arg->next) {
		struct chx_value *arge = cheax_eval(c, arg->value);

		/* won't set if args is NULL, so this ensures
		 * the GC won't delete our argument list */
		cheax_unref(c, args);

		cheax_ft(c, pad);

		*args_last = cheax_list(c, arge, NULL);
		args_last = &(*args_last)->next;

		cheax_ref(c, args);
	}

	cheax_unref(c, args);
	return args;

pad:
	return NULL;
}

static struct chx_value *
eval_sexpr(CHEAX *c, struct chx_list *input)
{
	struct chx_value *head = cheax_eval(c, input->value);
	cheax_ref(c, head);
	cheax_ft(c, pad);

	struct chx_list *args = input->next;

	struct chx_value *res = NULL;
	struct chx_ext_func *extf;
	struct chx_func *fn;
	struct chx_env *env;

	int ty = cheax_type_of(head);
	switch (ty) {
	case CHEAX_NIL:
		cry(c, "eval", CHEAX_ENIL, "cannot call nil");
		break;

	case CHEAX_EXT_FUNC:
		extf = (struct chx_ext_func *)head;
		res = extf->perform(c, input->next, extf->info);
		break;

	case CHEAX_FUNC:
	case CHEAX_MACRO:
		fn = (struct chx_func *)head;
		bool call_ok = false;

		if (ty == CHEAX_FUNC)
			args = eval_args(c, args);

		cheax_ref(c, args);

		struct chx_env *prev_env = c->env;
		cheax_ref(c, prev_env);
		c->env = fn->lexenv;
		cheax_push_env(c);

		bool arg_match_ok = cheax_match(c, fn->args, &args->base, CHEAX_READONLY);

		cheax_unref(c, args);

		if (!arg_match_ok) {
			cry(c, "eval", CHEAX_EMATCH, "invalid (number of) arguments");
			goto fn_pad;
		}

		struct chx_value *retval = NULL;
		for (struct chx_list *cons = fn->body; cons != NULL; cons = cons->next) {
			retval = cheax_eval(c, cons->value);
			cheax_ft(c, fn_pad);
		}

		call_ok = true;
		res = retval;
fn_pad:
		cheax_unref(c, prev_env);
		c->env = prev_env;
		if (call_ok && ty == CHEAX_MACRO)
			res = cheax_eval(c, res);
		break;

	case CHEAX_TYPECODE:
		; int type = ((struct chx_int *)head)->value;

		if (!cheax_is_valid_type(c, type)) {
			cry(c, "eval", CHEAX_ETYPE, "invalid typecode %d", type);
			break;
		}

		struct chx_list *cast_args = input->next;
		if (cast_args == NULL || cast_args->next != NULL) {
			cry(c, "eval", CHEAX_EMATCH, "expected single argument to cast");
			break;
		}

		struct chx_value *cast_arg = cast_args->value;

		if (cheax_get_base_type(c, type) != cheax_type_of(cast_arg)) {
			cry(c, "eval", CHEAX_ETYPE, "unable to instantiate");
			break;
		}

		res = cheax_cast(c, cast_arg, type);
		break;

	case CHEAX_ENV:
		env = (struct chx_env *)head;
		cheax_enter_env(c, env);

		struct chx_value *envval = NULL;
		for (struct chx_list *cons = args; cons != NULL; cons = cons->next) {
			envval = cheax_eval(c, cons->value);
			cheax_ft(c, env_pad);
		}

		res = envval;
env_pad:
		cheax_pop_env(c);
		break;

	default:
		cry(c, "eval", CHEAX_ETYPE, "invalid function call");
		break;
	}

pad:
	cheax_unref(c, head);
	return res;
}

static struct chx_value *
eval_bkquoted(CHEAX *c, struct chx_value *quoted, int nest)
{
	struct chx_value *res = NULL;

	struct chx_list *lst_quoted = NULL;
	struct chx_quote *qt_quoted = NULL;

	switch (cheax_type_of(quoted)) {
	case CHEAX_LIST:
		lst_quoted = (struct chx_list *)quoted;
		struct chx_value *car = eval_bkquoted(c, lst_quoted->value, nest);
		cheax_ref(c, car);
		struct chx_value *cdr = eval_bkquoted(c, &lst_quoted->next->base, nest);
		cheax_unref(c, car);
		cheax_ft(c, pad);
		res = &cheax_list(c, car, (struct chx_list *)cdr)->base;
		break;

	case CHEAX_QUOTE:
		qt_quoted = (struct chx_quote *)quoted;
		res = &cheax_quote(c, eval_bkquoted(c, qt_quoted->value, nest))->base;
		break;
	case CHEAX_BACKQUOTE:
		qt_quoted = (struct chx_quote *)quoted;
		res = &cheax_backquote(c, eval_bkquoted(c, qt_quoted->value, nest + 1))->base;
		break;

	case CHEAX_COMMA:
		qt_quoted = (struct chx_quote *)quoted;
		if (nest <= 0) {
			res = cheax_eval(c, qt_quoted->value);
		} else {
			struct chx_value *to_comma = eval_bkquoted(c, qt_quoted->value, nest - 1);
			cheax_ft(c, pad);
			res = &cheax_comma(c, to_comma)->base;
		}
		break;

	default:
		res = quoted;
		break;
	}

pad:
	return res;
}

struct chx_value *
cheax_eval(CHEAX *c, struct chx_value *input)
{
	struct chx_value *res = NULL;
	cheax_ref(c, input);

	switch (cheax_type_of(input)) {
	case CHEAX_ID:
		res = cheax_get(c, ((struct chx_id *)input)->id);
		break;

	case CHEAX_LIST:
		if (c->stack_depth >= c->max_stack_depth) {
			cry(c, "eval", CHEAX_ESTACK, "stack overflow! (maximum stack depth = %d)", c->max_stack_depth);
			break;
		}

		int prev_stack_depth = c->stack_depth++;
		res = eval_sexpr(c, (struct chx_list *)input);
		c->stack_depth = prev_stack_depth;

		break;

	case CHEAX_QUOTE:
		res = ((struct chx_quote *)input)->value;
		break;
	case CHEAX_BACKQUOTE:
		res = eval_bkquoted(c, ((struct chx_quote *)input)->value, 0);
		break;

	case CHEAX_COMMA:
		cry(c, "eval", CHEAX_EEVAL, "rogue comma");
		break;

	default:
		res = input;
		break;
	}

	cheax_ref(c, res);
	cheax_gc(c);
	cheax_unref(c, res);
	cheax_unref(c, input);
	return res;
}

