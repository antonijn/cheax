/* Copyright (c) 2023, Antonie Blom
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

#include <string.h>

#include "core.h"
#include "err.h"
#include "gc.h"
#include "unpack.h"

typedef struct chx_value (*value_op)(CHEAX *c, struct chx_value in);
static struct chx_list *list_map(CHEAX *c, value_op fn, struct chx_list *lst);
static struct chx_list *list_concat(CHEAX *c, struct chx_list *a, struct chx_list *b);

enum {
	BKQ_ERROR = -1,
	BKQ_VALUE,
	BKQ_SPLICED,
};

static int eval_bkquoted(CHEAX *c,
                         struct chx_value *value,
                         struct chx_list **spliced,
                         struct chx_value quoted,
                         int nest);

static void
rmshebang(FILE *f)
{
	char shebang[2] = { 0 };
	size_t bytes = fread(shebang, 1, 2, f);
	if (shebang[0] == '#' && shebang[1] == '!') {
		while (fgetc(f) != '\n')
			;
	} else {
		for (; bytes > 0; --bytes)
			ungetc(shebang[bytes - 1], f);
	}
}

void
cheax_exec(CHEAX *c, const char *path)
{
	ASSERT_NOT_NULL_VOID("exec", path);

	FILE *f = fopen(path, "rb");
	if (f == NULL) {
		cheax_throwf(c, CHEAX_EIO, "exec(): failed to open file \"%s\"", path);
		return;
	}

	rmshebang(f);

	int line = 1, pos = 0;
	for (;;) {
		struct chx_value v = cheax_read_at(c, f, path, &line, &pos);
		cheax_ft(c, pad);
		if (cheax_is_nil(v) && feof(f))
			break;

		v = cheax_preproc(c, v);
		cheax_ft(c, pad);
		cheax_eval(c, v);
		cheax_ft(c, pad);
	}

pad:
	fclose(f);
}

static struct chx_value
eval_ext_func(CHEAX *c, struct chx_ext_func *form, struct chx_list *args, bool eval_args)
{
	struct chx_list *true_args;
	struct chx_list arg_buf[16], *other_args = NULL;
	chx_ref ref_buf[16];
	size_t i = 0;
	struct chx_value res = CHEAX_NIL;

	if (eval_args) {
		struct chx_list **next_argp = &true_args;
		true_args = NULL;

		for (; args != NULL && i < sizeof(arg_buf) / sizeof(arg_buf[0]); args = args->next) {
			arg_buf[i].rtflags = 0;
			arg_buf[i].value = cheax_eval(c, args->value);
			arg_buf[i].next = NULL;

			cheax_ft(c, pad);

			ref_buf[i] = cheax_ref(c, arg_buf[i].value);

			*next_argp = &arg_buf[i];
			next_argp = &arg_buf[i++].next;
		}

		if (unpack(c, args, ".*", &other_args) < 0)
			goto pad;

		*next_argp = other_args;
	} else {
		true_args = args;
	}

	chx_ref other_arg_ref = cheax_ref_ptr(c, other_args);
	res = form->perform(c, true_args, form->info);
	cheax_unref_ptr(c, other_args, other_arg_ref);
pad:
	for (size_t j = 0; j < i; ++j)
		cheax_unref(c, arg_buf[j].value, ref_buf[j]);

	return res;
}

static int
eval_args(CHEAX *c,
          struct chx_func *fn,
          struct chx_list *args,
          struct chx_env *caller_env,
          bool argeval_override)
{
	int mflags = CHEAX_READONLY;
	if (!argeval_override)
		mflags |= CHEAX_EVAL_NODES;

	bool arg_match_ok = cheax_match_in(c, caller_env, fn->args, cheax_list_value(args), mflags);

	if (!arg_match_ok) {
		if (cheax_errno(c) == 0)
			cheax_throwf(c, CHEAX_EMATCH, "invalid (number of) arguments");
		cheax_add_bt(c);
		return -1;
	}

	return 0;
}

static int
eval_func_call(CHEAX *c,
               struct chx_func *fn,
               struct chx_list *args,
               struct chx_env *pop_stop,
               union chx_eval_out *out,
               bool argeval_override)
{
	struct chx_value res = CHEAX_NIL;

	struct chx_env *caller_env = c->env;

	/* set up callee env */
	c->env = fn->lexenv;
	cheax_push_env(c);
	if (cheax_errno(c) != 0) {
		cheax_add_bt(c);
		goto env_fail_pad;
	}

	chx_ref caller_env_ref = cheax_ref_ptr(c, caller_env);

	if (eval_args(c, fn, args, caller_env, argeval_override) < 0)
		goto pad;

	cheax_unref_ptr(c, caller_env, caller_env_ref);

	struct chx_env *func_env = c->env;
	c->env = caller_env;

	while (c->env != pop_stop)
		cheax_pop_env(c);

	c->env = func_env;

	chx_ref ps_ref = cheax_ref_ptr(c, pop_stop);

	if (fn->body != NULL) {
		struct chx_list *stat;
		for (stat = fn->body; stat->next != NULL; stat = stat->next) {
			bt_wrap(c, cheax_eval(c, stat->value));
			cheax_ft(c, pad2);
		}

		out->ts.tail = stat->value;
	} else {
		out->ts.tail = CHEAX_NIL;
	}
pad2:
	cheax_unref_ptr(c, pop_stop, ps_ref);
	out->ts.pop_stop = fn->lexenv;
	return CHEAX_TAIL_OUT;

pad:
	cheax_pop_env(c);
	cheax_unref_ptr(c, caller_env, caller_env_ref);
env_fail_pad:
	c->env = caller_env;

	out->value = res;
	return CHEAX_VALUE_OUT;
}

static int
eval_env_call(CHEAX *c,
              struct chx_env *env,
              struct chx_list *args,
              struct chx_env *pop_stop,
              union chx_eval_out *out)
{
	cheax_enter_env(c, env);
	if (cheax_errno(c) != 0) {
		out->value = bt_wrap(c, CHEAX_NIL);
		return CHEAX_VALUE_OUT;
	}

	struct chx_value res = CHEAX_NIL;
	for (struct chx_list *cons = args; cons != NULL; cons = cons->next) {
		res = bt_wrap(c, cheax_eval(c, cons->value));
		cheax_ft(c, env_pad);
	}
env_pad:
	cheax_pop_env(c);
	out->value = res;
	return CHEAX_VALUE_OUT;
}

static int
eval_cast(CHEAX *c,
          chx_int head,
          struct chx_list *args,
          struct chx_env *pop_stop,
          union chx_eval_out *out)
{
	struct chx_value cast_arg;
	out->value = (0 == unpack(c, args, ".", &cast_arg))
	           ? bt_wrap(c, cheax_cast(c, cast_arg, head))
	           : CHEAX_NIL;
	return CHEAX_VALUE_OUT;
}

static int
eval_sexpr(CHEAX *c, struct chx_list *input, struct chx_env *pop_stop, union chx_eval_out *out)
{
	if (c->stack_limit > 0 && c->stack_depth >= c->stack_limit) {
		cheax_throwf(c, CHEAX_ESTACK, "stack overflow! (stack limit %d)", c->stack_limit);
		out->value = CHEAX_NIL;
		return CHEAX_VALUE_OUT;
	}

	int res = CHEAX_VALUE_OUT;
	int prev_stack_depth = c->stack_depth++;

	chx_ref input_ref = cheax_ref_ptr(c, input);

	struct chx_value head = cheax_eval(c, input->value);
	cheax_ft(c, pad);

	chx_ref head_ref = cheax_ref(c, head);

	/* should already be cheax_ref()'d further up the
	 * call chain; no need here */
	struct chx_list *was_last_call = c->bt.last_call;
	c->bt.last_call = input;

	struct chx_list *args = input->next;

	switch (head.type) {
	case CHEAX_EXT_FUNC:
		out->value = eval_ext_func(c, head.data.as_ext_func, args, true);
		res = CHEAX_VALUE_OUT;
		break;

	case CHEAX_SPECIAL_OP:
		res = head.data.as_special_op->perform(c,
		                                       args,
		                                       head.data.as_special_op->info,
		                                       pop_stop,
		                                       out);
		break;

	case CHEAX_FUNC:
		res = eval_func_call(c, head.data.as_func, args, pop_stop, out, false);
		break;

	case CHEAX_TYPECODE:
		res = eval_cast(c, head.data.as_int, args, pop_stop, out);
		break;

	case CHEAX_ENV:
		res = eval_env_call(c, head.data.as_env, args, pop_stop, out);
		break;

	default:
		cheax_throwf(c, CHEAX_ETYPE, "invalid function call");
		out->value = bt_wrap(c, CHEAX_NIL);
		res = CHEAX_VALUE_OUT;
		break;
	}

	cheax_unref(c, head, head_ref);

	if (res == CHEAX_VALUE_OUT) {
		c->bt.last_call = was_last_call;
		chx_ref res_ref = cheax_ref(c, out->value);
		cheax_gc(c);
		cheax_unref(c, out->value, res_ref);
	}
pad:
	cheax_unref_ptr(c, input, input_ref);
	c->stack_depth = prev_stack_depth;
	return res;
}

static struct chx_list *
list_map(CHEAX *c, value_op fn, struct chx_list *lst)
{
	struct chx_list *res = NULL, **nxt = &res;
	for (; lst !=  NULL; lst = lst->next) {
		*nxt = cheax_list(c, fn(c, lst->value), NULL).data.as_list;
		cheax_ft(c, pad);
		nxt = &(*nxt)->next;
	}
	return res;
pad:
	return NULL;
}

static struct chx_list *
list_concat(CHEAX *c, struct chx_list *a, struct chx_list *b)
{
	if (a == NULL)
		return b;

	struct chx_list *cdr = list_concat(c, a->next, b);
	cheax_ft(c, pad);
	return cheax_list(c, a->value, cdr).data.as_list;
pad:
	return NULL;
}

static struct chx_list *
eval_bkquoted_list(CHEAX *c, struct chx_list *quoted, int nest)
{
	if (quoted == NULL)
		return NULL;

	struct chx_value car;
	struct chx_list *spl_list, *cdr;
	struct chx_value spl_list_value;
	chx_ref car_ref, spl_list_ref;
	switch (eval_bkquoted(c, &car, &spl_list, quoted->value, nest)) {
	case BKQ_VALUE:
		car_ref = cheax_ref(c, car);
		cdr = eval_bkquoted_list(c, quoted->next, nest);
		cheax_unref(c, car, car_ref);
		cheax_ft(c, pad);

		return orig_debug_list(c, car, (struct chx_list *)cdr, quoted);

	case BKQ_SPLICED:
		spl_list_value = cheax_list_value(spl_list);
		spl_list_ref = cheax_ref(c, spl_list_value);
		cdr = eval_bkquoted_list(c, quoted->next, nest);
		cheax_unref(c, spl_list_value, spl_list_ref);
		cheax_ft(c, pad);

		return (cdr == NULL) ? spl_list : list_concat(c, spl_list, cdr);
	}
pad:
	return NULL;
}

static int
expand_comma(CHEAX *c,
             struct chx_value *value,
             struct chx_list **spliced,
             struct chx_value quoted)
{
	struct chx_value evald = cheax_eval(c, quoted.data.as_quote->value);
	cheax_ft(c, pad);

	switch (quoted.type) {
	case CHEAX_COMMA:
		*value = evald;
		return BKQ_VALUE;

	case CHEAX_SPLICE:
		if (!cheax_is_nil(evald) && evald.type != CHEAX_LIST) {
			cheax_throwf(c, CHEAX_EEVAL, "expected list after ,@");
			return BKQ_ERROR;
		}

		if (spliced != NULL)
			*spliced = evald.data.as_list;
		return BKQ_SPLICED;
	}
pad:
	return BKQ_ERROR;
}

static int
eval_bkquoted_comma(CHEAX *c,
                    struct chx_value *value,
                    struct chx_list **spliced,
                    struct chx_value quoted,
                    int nest)
{
	if (nest <= 0)
		return expand_comma(c, value, spliced, quoted);

	struct chx_value to_comma;
	struct chx_list *splice_to_comma;
	value_op comma = (value_op)((quoted.type == CHEAX_COMMA) ? cheax_comma : cheax_splice);
	switch (eval_bkquoted(c, &to_comma, &splice_to_comma, quoted.data.as_quote->value, nest - 1)) {
	case BKQ_VALUE:
		*value = comma(c, to_comma);
		cheax_ft(c, pad);
		return BKQ_VALUE;

	case BKQ_SPLICED:
		if (spliced != NULL) {
			*spliced = list_map(c, comma, splice_to_comma);
			cheax_ft(c, pad);
		}

		return BKQ_SPLICED;
	}
pad:
	return BKQ_ERROR;
}

static int
eval_bkquoted(CHEAX *c,
              struct chx_value *value,
              struct chx_list **spliced,
              struct chx_value quoted,
              int nest)
{
	struct chx_quote *qt_quoted = NULL;
	int code;
	bool bkquote = false;

	switch (quoted.type) {
	case CHEAX_LIST:
		*value = cheax_list_value(eval_bkquoted_list(c, quoted.data.as_list, nest));
		return BKQ_VALUE;

	case CHEAX_BACKQUOTE:
		bkquote = true;
		/* fall through */
	case CHEAX_QUOTE:
		qt_quoted = (struct chx_quote *)quoted.data.as_quote;
		struct chx_value to_quote;

		code = eval_bkquoted(c, &to_quote, NULL, qt_quoted->value, nest + bkquote);
		if (code != BKQ_VALUE) {
			if (code == BKQ_SPLICED) {
				cheax_throwf(c,
				             CHEAX_EEVAL,
				             "%s expects one argument",
				             bkquote ? "backquote" : "quote");
			}
			return BKQ_ERROR;
		}

		*value = (bkquote ? cheax_backquote : cheax_quote)(c, to_quote);
		cheax_ft(c, pad);
		return BKQ_VALUE;

	case CHEAX_COMMA:
	case CHEAX_SPLICE:
		return eval_bkquoted_comma(c, value, spliced, quoted, nest);

	default:
		*value = quoted;
		return BKQ_VALUE;
	}
pad:
	return BKQ_ERROR;
}

static int
eval(CHEAX *c, struct chx_value input, struct chx_env *pop_stop, union chx_eval_out *out)
{
	struct chx_value res = CHEAX_NIL;
	chx_ref input_ref;
	int code;

	switch (input.type) {
	case CHEAX_ID:
		res = cheax_get(c, input.data.as_id->value);
		break;

	case CHEAX_LIST:
		if (input.data.as_list == NULL)
			break;

		return eval_sexpr(c, input.data.as_list, pop_stop, out);

	case CHEAX_QUOTE:
		res = input.data.as_quote->value;
		break;
	case CHEAX_BACKQUOTE:
		input_ref = cheax_ref(c, input);
		code = eval_bkquoted(c, &res, NULL, input.data.as_quote->value, 0);
		if (code == BKQ_SPLICED)
			cheax_throwf(c, CHEAX_EEVAL, "internal splice error");
		cheax_unref(c, input, input_ref);

		chx_ref res_ref = cheax_ref(c, res);
		cheax_gc(c);
		cheax_unref(c, res, res_ref);
		break;

	case CHEAX_COMMA:
		cheax_throwf(c, CHEAX_EEVAL, "rogue comma");
		break;
	case CHEAX_SPLICE:
		cheax_throwf(c, CHEAX_EEVAL, "rogue ,@");
		break;

	default:
		res = input;
		break;
	}

	out->value = res;
	return CHEAX_VALUE_OUT;
}

typedef int (*tail_evaluator)(CHEAX *c,
                              void *input_info,
                              struct chx_env *pop_stop,
                              union chx_eval_out *out);

static int
value_evaluator(CHEAX *c, void *input_info, struct chx_env *pop_stop, union chx_eval_out *out)
{
	struct chx_value val = *(struct chx_value *)input_info;
	return eval(c, val, pop_stop, out);
}

static int
apply_func_evaluator(CHEAX *c, void *input_info, struct chx_env *pop_stop, union chx_eval_out *out)
{
	struct chx_list afi = *(struct chx_list *)input_info;
	struct chx_value func = afi.value;
	struct chx_list *args = afi.next;

	int res;
	switch (func.type) {
	case CHEAX_EXT_FUNC:
		out->value = eval_ext_func(c, func.data.as_ext_func, args, false);
		res = CHEAX_VALUE_OUT;
		break;

	case CHEAX_FUNC:
		res = eval_func_call(c, func.data.as_func, args, pop_stop, out, true);
		break;

	default:
		cheax_throwf(c, CHEAX_EEVAL, "apply_func_evaluator(): internal error");
		out->value = CHEAX_NIL;
		res = CHEAX_VALUE_OUT;
		break;
	}

	return res;
}

static struct chx_value
wrap_tail_eval(CHEAX *c, tail_evaluator initial_eval, void *input_info)
{
	union chx_eval_out out = { 0 };
	struct chx_env *ret_env = c->env;
	struct chx_list *ret_last_call = c->bt.last_call;

	if (initial_eval(c, input_info, c->env, &out) == CHEAX_VALUE_OUT)
		return out.value;

	struct chx_list *was_last_call = c->bt.last_call;
	int tail_lvls = -1;
	struct chx_value res = CHEAX_NIL;
	cheax_ft(c, pad);

	/* we've leapt into a tail call */
	chx_ref last_call_ref = cheax_ref_ptr(c, was_last_call);
	chx_ref ret_last_call_ref = cheax_ref_ptr(c, ret_last_call);
	chx_ref ret_env_ref = cheax_ref_ptr(c, ret_env);

	struct chx_env *pop_stop;

	if (c->tail_call_elimination) {
		int ek;
		do {
			++tail_lvls;
			pop_stop = out.ts.pop_stop;
			ek = eval(c, out.ts.tail, pop_stop, &out);
			cheax_ft(c, pad2);
		} while (ek == CHEAX_TAIL_OUT);
	} else {
		pop_stop = out.ts.pop_stop;
		out.value = cheax_eval(c, out.ts.tail);
	}

	while (c->env != pop_stop)
		cheax_pop_env(c);

pad2:
	cheax_unref_ptr(c, ret_env, ret_env_ref);
	cheax_unref_ptr(c, was_last_call, last_call_ref);
	cheax_unref_ptr(c, ret_last_call, ret_last_call_ref);
pad:
	if (cheax_errno(c) != 0) {
		if (c->tail_call_elimination)
			bt_add_tail_msg(c, tail_lvls);
		c->bt.last_call = was_last_call;
		cheax_add_bt(c);
	} else {
		res = out.value;
	}

	c->env = ret_env;
	c->bt.last_call = ret_last_call;
	return res;
}

struct chx_value
cheax_eval(CHEAX *c, struct chx_value input)
{
	return wrap_tail_eval(c, value_evaluator, &input);
}

struct chx_value
cheax_macroexpand(CHEAX *c, struct chx_value expr)
{
	struct chx_value expr_mexp = expr;
	do {
		expr = expr_mexp;
		expr_mexp = cheax_macroexpand_once(c, expr);
		cheax_ft(c, pad);
	} while (!cheax_equiv(expr, expr_mexp));
pad:
	return expr_mexp;
}

struct chx_value
cheax_macroexpand_once(CHEAX *c, struct chx_value expr)
{
	if (expr.type != CHEAX_LIST || expr.data.as_list == NULL)
		return expr;

	struct chx_list *lst = expr.data.as_list;
	struct chx_value head = lst->value, macro;
	if (head.type != CHEAX_ID
	 || !cheax_try_get_from(c, &c->macro_ns, head.data.as_id->value, &macro))
	{
		return expr;
	}

	if (macro.type != CHEAX_FUNC && macro.type != CHEAX_EXT_FUNC) {
		cheax_throwf(c, CHEAX_ESTATIC, "invalid macro type");
		return CHEAX_NIL;
	}

	chx_ref lst_ref = cheax_ref_ptr(c, lst);

	struct chx_value res = cheax_apply(c, macro, lst->next);

	cheax_unref_ptr(c, lst, lst_ref);
	cheax_ft(c, pad);

	if (res.type == CHEAX_LIST && res.data.as_list != NULL && c->gen_debug_info) {
		struct chx_list *exp_lst = res.data.as_list;
		res = cheax_list_value(orig_debug_list(c, exp_lst->value, exp_lst->next, lst));
	}

	return res;
pad:
	return CHEAX_NIL;
}

static bool
should_preprocess(struct chx_value expr)
{
	return expr.type == CHEAX_LIST
	    && expr.data.as_list != NULL
	    && !has_flag(expr.data.as_list->rtflags, PREPROC_BIT);
}

static struct chx_value
preproc_specop(CHEAX *c, const char *id, struct chx_value specop_val, struct chx_list *call)
{
	struct chx_list *tail = call->next;

	if (specop_val.type != CHEAX_SPECIAL_OP) {
		cheax_throwf(c, CHEAX_ESTATIC, "corrupted special operation `%s'", id);
		goto pad;
	}

	struct chx_special_op *specop = specop_val.data.as_special_op;

	struct chx_ext_func pp_func = { 0, specop->name, specop->preproc, specop->info };
	struct chx_value out_tail = cheax_apply(c, cheax_ext_func_value(&pp_func), tail);
	cheax_ft(c, pad);

	if (out_tail.type != CHEAX_LIST) {
		cheax_throwf(c, CHEAX_ESTATIC, "preprocessing for `%s' did not yield list", id);
		goto pad;
	}

	struct chx_list *out_list = orig_debug_list(c, specop_val, out_tail.data.as_list, call);
	if (out_list != NULL)
		out_list->rtflags |= PREPROC_BIT;
	return cheax_list_value(out_list);
pad:
	return CHEAX_NIL;
}

static struct chx_value
preproc_fcall(CHEAX *c, struct chx_list *call)
{
	static const uint8_t ops[] = { PP_SEQ, PP_EXPR, };
	struct chx_value call_out_val = preproc_pattern(c, cheax_list_value(call), ops, NULL);
	cheax_ft(c, pad);

	if (call_out_val.type != CHEAX_LIST) {
		cheax_throwf(c, CHEAX_ESTATIC, "preproc_fcall(): eek, what happened?");
		return CHEAX_NIL;
	}

	struct chx_list *call_out = call_out_val.data.as_list;
	if (call_out != NULL)
		call_out->rtflags |= PREPROC_BIT;
pad:
	return call_out_val;
}

struct chx_value
cheax_preproc(CHEAX *c, struct chx_value expr)
{
	if (!should_preprocess(expr))
		return expr;

	struct chx_value mac_exp = cheax_macroexpand(c, expr);

	if (!cheax_equiv(mac_exp, expr)) {
		/* Macro expansion actually did something */
		if (!should_preprocess(mac_exp))
			return mac_exp;

		expr = mac_exp;
	}

	chx_ref expr_ref = cheax_ref(c, expr);

	/* We could be dealing with a special form or a regular function
	 * call */

	struct chx_value head = expr.data.as_list->value;

	struct chx_value specop, out;
	if (head.type == CHEAX_ID
	 && cheax_try_get_from(c, &c->specop_ns, head.data.as_id->value, &specop))
	{
		out = preproc_specop(c, head.data.as_id->value, specop, expr.data.as_list);
	}
	else
	{
		out = preproc_fcall(c, expr.data.as_list);
	}

	cheax_unref(c, expr, expr_ref);
	return out;
}

struct chx_value
cheax_apply(CHEAX *c, struct chx_value func, struct chx_list *args)
{
	struct chx_list afi = { 0, func, args };
	chx_ref func_ref, args_ref;

	switch (func.type) {
	case CHEAX_EXT_FUNC:
	case CHEAX_FUNC:
		func_ref = cheax_ref(c, func);
		args_ref = cheax_ref_ptr(c, args);
		struct chx_value res = wrap_tail_eval(c, apply_func_evaluator, &afi);
		cheax_unref(c, func, func_ref);
		cheax_unref_ptr(c, args, args_ref);
		return res;

	default:
		cheax_throwf(c, CHEAX_ETYPE, "apply(): only ExtFunc and Func allowed (got type %d)", func.type);
		return CHEAX_NIL;
	}
}

static bool
match_node(CHEAX *c,
           struct chx_env *env,
           struct chx_value pan_node,
           struct chx_value match_node,
           int flags)
{
	if (has_flag(flags, CHEAX_EVAL_NODES)) {
		struct chx_env *prev_env = c->env;
		chx_ref prev_env_ref = cheax_ref_ptr(c, prev_env);
		c->env = env;

		match_node = cheax_eval(c, match_node);

		cheax_unref_ptr(c, prev_env, prev_env_ref);
		c->env = prev_env;
		cheax_ft(c, pad);
	}

	return cheax_match_in(c, env, pan_node, match_node, flags & ~CHEAX_EVAL_NODES);
pad:
	return false;
}

static bool
match_colon(CHEAX *c,
            struct chx_env *env,
            struct chx_list *pan,
            struct chx_list *match,
            int flags)
{
	if (pan->next == NULL)
		return cheax_match_in(c, env, pan->value, cheax_list_value(match), flags);

	return match != NULL
	    && match_node(c, env, pan->value, match->value, flags)
	    && match_colon(c, env, pan->next, match->next, flags);
}

static bool
match_list(CHEAX *c, struct chx_env *env, struct chx_list *pan, struct chx_list *match, int flags)
{
	if (pan != NULL && pan->value.type == CHEAX_ID
	 && strcmp(pan->value.data.as_id->value, ":") == 0)
	{
		return match_colon(c, env, pan->next, match, flags);
	}

	while (pan != NULL && match != NULL) {
		if (!match_node(c, env, pan->value, match->value, flags))
			return false;

		pan = pan->next;
		match = match->next;
	}

	return (pan == NULL) && (match == NULL);
}

bool
cheax_match_in(CHEAX *c,
               struct chx_env *env,
               struct chx_value pan,
               struct chx_value match,
               int flags)
{
	if (pan.type == CHEAX_ID) {
		if (has_flag(flags, CHEAX_EVAL_NODES) && match.type == CHEAX_LIST) {
			struct chx_env *prev_env = c->env;
			chx_ref prev_env_ref = cheax_ref_ptr(c, prev_env);
			c->env = env;

			struct chx_list *evald_match;
			int res = unpack(c, match.data.as_list, ".*", &evald_match);
			match = cheax_list_value(evald_match);

			cheax_unref_ptr(c, prev_env, prev_env_ref);
			c->env = prev_env;
			if (res < 0)
				return false;
		}

		cheax_def(c, pan.data.as_id->value, match, flags);
		return cheax_errno(c) == 0; /* false if cheax_def() failed */
	}

	if (pan.type != match.type)
		return false;

	switch (pan.type) {
	case CHEAX_LIST:
		return match_list(c, env, pan.data.as_list, match.data.as_list, flags);
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
cheax_match(CHEAX *c, struct chx_value pan, struct chx_value match, int flags)
{
	return cheax_match_in(c, c->env, pan, match, flags);
}

static bool
list_eq(CHEAX *c, struct chx_list *l, struct chx_list *r)
{
	while ((l != NULL) && (r != NULL)) {
		if (!cheax_eq(c, l->value, r->value))
			return false;

		l = l->next;
		r = r->next;
	}

	return (l == NULL) && (r == NULL);
}

bool
cheax_eq(CHEAX *c, struct chx_value l, struct chx_value r)
{
	if (l.type != r.type)
		return false;

	switch (cheax_resolve_type(c, l.type)) {
	case CHEAX_ID:
		return 0 == strcmp(l.data.as_id->value, r.data.as_id->value);
	case CHEAX_INT:
		return l.data.as_int == r.data.as_int;
	case CHEAX_BOOL:
		return (l.data.as_int != 0) == (r.data.as_int != 0);
	case CHEAX_DOUBLE:
		return l.data.as_double == r.data.as_double;
	case CHEAX_LIST:
		return list_eq(c, l.data.as_list, r.data.as_list);
	case CHEAX_EXT_FUNC:
		return (l.data.as_ext_func->perform == r.data.as_ext_func->perform)
		    && (l.data.as_ext_func->info == r.data.as_ext_func->info);
	case CHEAX_QUOTE:
	case CHEAX_BACKQUOTE:
	case CHEAX_COMMA:
	case CHEAX_SPLICE:
		return cheax_eq(c, l.data.as_quote->value, l.data.as_quote->value);
	case CHEAX_STRING:
		return (l.data.as_string->len == r.data.as_string->len)
		    && (0 == memcmp(l.data.as_string->value,
		                    r.data.as_string->value,
		                    r.data.as_string->len));
	case CHEAX_USER_PTR:
	default:
		return l.data.user_ptr == r.data.user_ptr;
	}
}

bool
cheax_equiv(struct chx_value l, struct chx_value r)
{
	return l.type == r.type && l.data.as_list == r.data.as_list;
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
bltn_eval(CHEAX *c, struct chx_list *args, void *info)
{
	struct chx_value arg;
	return (0 == unpack(c, args, "_", &arg))
	     ? bt_wrap(c, cheax_eval(c, arg))
	     : CHEAX_NIL;
}

static struct chx_value
bltn_apply(CHEAX *c, struct chx_list *args, void *info)
{
	/* TODO revert back to tailable form */

	struct chx_value func;
	struct chx_list *list;
	if (unpack(c, args, "[LP]C", &func, &list) < 0)
		return CHEAX_NIL;

	return cheax_apply(c, func, list);
}

static struct chx_value
bltn_eq(CHEAX *c, struct chx_list *args, void *info)
{
	struct chx_value l, r;
	return (0 == unpack(c, args, "__", &l, &r))
	     ? bt_wrap(c, cheax_bool(cheax_eq(c, l, r)))
	     : CHEAX_NIL;
}

static struct chx_value
bltn_ne(CHEAX *c, struct chx_list *args, void *info)
{
	struct chx_value l, r;
	return (0 == unpack(c, args, "__", &l, &r))
	     ? bt_wrap(c, cheax_bool(!cheax_eq(c, l, r)))
	     : CHEAX_NIL;
}

static int
sf_case(CHEAX *c,
        struct chx_list *args,
        void *info,
        struct chx_env *pop_stop,
        union chx_eval_out *out)
{
	if (args == NULL) {
		cheax_throwf(c, CHEAX_EMATCH, "invalid case");
		out->value = bt_wrap(c, CHEAX_NIL);
		return CHEAX_VALUE_OUT;
	}

	struct chx_value what = cheax_eval(c, args->value);
	cheax_ft(c, pad);

	/* pattern-value-pair */
	for (struct chx_list *pvp = args->next; pvp != NULL; pvp = pvp->next) {
		struct chx_value pair = pvp->value;
		if (pair.type != CHEAX_LIST || pair.data.as_list == NULL) {
			cheax_throwf(c, CHEAX_EMATCH, "pattern-value pair expected");
			out->value = bt_wrap(c, CHEAX_NIL);
			return CHEAX_VALUE_OUT;
		}

		struct chx_list *cons_pair = pair.data.as_list;
		struct chx_value pan = cons_pair->value;

		cheax_push_env(c);
		cheax_ft(c, pad);

		if (!cheax_match(c, pan, what, CHEAX_READONLY)) {
			cheax_pop_env(c);
			continue;
		}

		/* pattern matches! */

		if (cons_pair->next == NULL)
			goto pad;

		struct chx_list *stat;
		for (stat = cons_pair->next; stat->next != NULL; stat = stat->next) {
			bt_wrap(c, cheax_eval(c, stat->value));
			cheax_ft(c, pad2);
		}

		out->ts.tail = stat->value;
		out->ts.pop_stop = pop_stop;
		return CHEAX_TAIL_OUT;
pad2:
		cheax_pop_env(c);
		goto pad;
	}

	cheax_throwf(c, CHEAX_EMATCH, "non-exhaustive pattern");
	cheax_add_bt(c);
pad:
	out->value = CHEAX_NIL;
	return CHEAX_VALUE_OUT;
}

static struct chx_value
pp_sf_case(CHEAX *c, struct chx_list *args, void *info)
{
	/* (node EXPR (seq (node LIT (seq EXPR)))) */
	static const uint8_t ops[] = {
		PP_NODE | PP_ERR(0), PP_EXPR, PP_SEQ, PP_NODE | PP_ERR(1), PP_LIT, PP_SEQ, PP_EXPR,
	};

	static const char *errors[] = {
		"expected value",
		"pattern-value pair expected",
	};

	return preproc_pattern(c, cheax_list_value(args), ops, errors);
}

static int
sf_cond(CHEAX *c,
        struct chx_list *args,
        void *info,
        struct chx_env *pop_stop,
        union chx_eval_out *out)
{
	/* test-value-pair */
	for (struct chx_list *tvp = args; tvp != NULL; tvp = tvp->next) {
		struct chx_value pair = tvp->value;
		if (pair.type != CHEAX_LIST || pair.data.as_list == NULL) {
			cheax_throwf(c, CHEAX_EMATCH, "test-value pair expected");
			goto pad;
		}

		struct chx_list *cons_pair = pair.data.as_list;
		struct chx_value test = cons_pair->value;

		test = cheax_eval(c, test);
		cheax_ft(c, pad);
		if (test.type != CHEAX_BOOL) {
			cheax_throwf(c, CHEAX_ETYPE, "test must have boolean value");
			cheax_add_bt(c);
			break;
		}

		if (test.data.as_int == 0)
			continue;

		/* we have a match! */
		cheax_push_env(c);
		cheax_ft(c, pad);

		if (cons_pair->next == NULL)
			goto pad2;

		struct chx_list *stat;
		for (stat = cons_pair->next; stat->next != NULL; stat = stat->next) {
			bt_wrap(c, cheax_eval(c, stat->value));
			cheax_ft(c, pad2);
		}

		out->ts.tail = stat->value;
		out->ts.pop_stop = pop_stop;
		return CHEAX_TAIL_OUT;
pad2:
		cheax_pop_env(c);
		break;
	}

pad:
	out->value = CHEAX_NIL;
	return CHEAX_VALUE_OUT;
}

static struct chx_value
pp_sf_cond(CHEAX *c, struct chx_list *args, void *info)
{
	/* (seq (node EXPR (seq EXPR))) */
	static const uint8_t ops[] = {
		PP_SEQ, PP_NODE | PP_ERR(0), PP_EXPR, PP_SEQ, PP_EXPR,
	};
	static const char *errors[] = { "test-value pair expected" };
	return preproc_pattern(c, cheax_list_value(args), ops, errors);
}

void
export_eval_bltns(CHEAX *c)
{
	cheax_defun(c, "eval",  bltn_eval,  NULL);
	cheax_defun(c, "apply", bltn_apply, NULL);
	cheax_defun(c, "=",     bltn_eq,    NULL);
	cheax_defun(c, "!=",    bltn_ne,    NULL);

	cheax_defsyntax(c, "case", sf_case, pp_sf_case, NULL);
	cheax_defsyntax(c, "cond", sf_cond, pp_sf_cond, NULL);
}
