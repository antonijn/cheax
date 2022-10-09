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

void
cheax_exec(CHEAX *c, const char *path)
{
	ASSERT_NOT_NULL_VOID("exec", path);

	FILE *f = fopen(path, "rb");
	if (f == NULL) {
		cheax_throwf(c, CHEAX_EIO, "exec(): failed to open file \"%s\"", path);
		return;
	}

	char shebang[2] = { 0 };
	fread(shebang, 2, 1, f);
	if (shebang[0] == '#' && shebang[1] == '!') {
		while (fgetc(f) != '\n')
			;
	} else {
		ungetc(shebang[1], f);
		ungetc(shebang[0], f);
	}

	int line = 1, pos = 0;

	struct chx_value v;
	while (!cheax_is_nil(v = cheax_read_at(c, f, path, &line, &pos))) {
		cheax_ft(c, pad);
		cheax_eval(c, v);
		cheax_ft(c, pad);
	}

pad:
	fclose(f);
}

static int
eval_args(CHEAX *c,
          struct chx_value fn_value,
          struct chx_list *args,
          struct chx_env *caller_env,
          bool argeval_override)
{
	struct chx_func *fn = fn_value.data.as_func;

	int mflags = CHEAX_READONLY;
	if (fn_value.type == CHEAX_FUNC && !argeval_override)
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
               struct chx_value fn_value,
               struct chx_list *args,
               struct chx_env *pop_stop,
               union chx_eval_out *out,
               bool argeval_override)
{
	struct chx_func *fn = fn_value.data.as_func;
	struct chx_value res = cheax_nil();

	bool call_ok = false;
	int ty = fn_value.type;

	struct chx_env *caller_env = c->env;

	/* set up callee env */
	c->env = fn->lexenv;
	cheax_push_env(c);
	if (cheax_errno(c) != 0) {
		cheax_add_bt(c);
		goto env_fail_pad;
	}

	chx_ref caller_env_ref = cheax_ref_ptr(c, caller_env);

	if (eval_args(c, fn_value, args, caller_env, argeval_override) < 0)
		goto pad;

	/* we have a license to pop the caller context */
	if (ty == CHEAX_FUNC) {
		struct chx_env *func_env = c->env;
		c->env = caller_env;

		while (c->env != pop_stop)
			cheax_pop_env(c);

		c->env = func_env;
	}

	if (ty == CHEAX_FUNC && fn->body != NULL) {
		struct chx_list *stat;
		for (stat = fn->body; stat->next != NULL; stat = stat->next) {
			bt_wrap(c, cheax_eval(c, stat->value));
			cheax_ft(c, pad);
		}

		cheax_unref_ptr(c, caller_env, caller_env_ref);
		out->ts.pop_stop = fn->lexenv;
		out->ts.tail = stat->value;
		return CHEAX_TAIL_OUT;
	}

	for (struct chx_list *cons = fn->body; cons != NULL; cons = cons->next) {
		res = bt_wrap(c, cheax_eval(c, cons->value));
		cheax_ft(c, pad);
	}

	call_ok = true;
pad:
	cheax_pop_env(c);
	cheax_unref_ptr(c, caller_env, caller_env_ref);
env_fail_pad:
	c->env = caller_env;
	if (call_ok && ty == CHEAX_MACRO) {
		out->ts.tail = res;
		out->ts.pop_stop = pop_stop;
		return CHEAX_TAIL_OUT;
	}

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
		out->value = bt_wrap(c, cheax_nil());
		return CHEAX_VALUE_OUT;
	}

	struct chx_value res = cheax_nil();
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
	           : cheax_nil();
	return CHEAX_VALUE_OUT;
}

static int
eval_sexpr(CHEAX *c, struct chx_list *input, struct chx_env *pop_stop, union chx_eval_out *out)
{
	if (c->stack_limit > 0 && c->stack_depth >= c->stack_limit) {
		cheax_throwf(c, CHEAX_ESTACK, "stack overflow! (stack limit %d)", c->stack_limit);
		out->value = cheax_nil();
		return CHEAX_VALUE_OUT;
	}

	int res;
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
	case CHEAX_SPECIAL_FORM:
		out->value = head.data.as_special_form->perform.func(c,
		                                                     args,
		                                                     head.data.as_special_form->info);
		res = CHEAX_VALUE_OUT;
		break;
	case CHEAX_SPECIAL_TAIL_FORM:
		res = head.data.as_special_form->perform.tail_func(c,
		                                                   args,
		                                                   head.data.as_special_form->info,
		                                                   pop_stop,
		                                                   out);
		break;

	case CHEAX_FUNC:
	case CHEAX_MACRO:
		res = eval_func_call(c, head, args, pop_stop, out, false);
		break;

	case CHEAX_TYPECODE:
		res = eval_cast(c, head.data.as_int, args, pop_stop, out);
		break;

	case CHEAX_ENV:
		res = eval_env_call(c, head.data.as_env, args, pop_stop, out);
		break;

	default:
		cheax_throwf(c, CHEAX_ETYPE, "invalid function call");
		out->value = bt_wrap(c, cheax_nil());
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

		if (has_flag(quoted->rtflags, DEBUG_LIST)) {
			struct debug_info info = ((struct debug_list *)quoted)->info;
			return &debug_list(c, car, (struct chx_list *)cdr, info)->base;
		}

		return cheax_list(c, car, (struct chx_list *)cdr).data.as_list;

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
	struct chx_value res = cheax_nil();
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

struct apply_func_info {
	struct chx_value func;
	struct chx_list *args;
};

static int
apply_func_evaluator(CHEAX *c, void *input_info, struct chx_env *pop_stop, union chx_eval_out *out)
{
	struct apply_func_info afi = *(struct apply_func_info *)input_info;
	struct chx_value func = afi.func;
	struct chx_list *args = afi.args;

	int res;
	switch (func.type) {
	case CHEAX_SPECIAL_FORM:
		out->value = func.data.as_special_form->perform.func(c,
		                                                     args,
		                                                     func.data.as_special_form->info);
		res = CHEAX_VALUE_OUT;
		break;
	case CHEAX_SPECIAL_TAIL_FORM:
		res = func.data.as_special_form->perform.tail_func(c,
		                                                   args,
		                                                   func.data.as_special_form->info,
		                                                   pop_stop,
		                                                   out);
		break;

	case CHEAX_FUNC:
		res = eval_func_call(c, func, args, pop_stop, out, true);
		break;

	default:
		cheax_throwf(c, CHEAX_EEVAL, "apply_func_evaluator(): internal error");
		out->value = cheax_nil();
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

	struct chx_value res = cheax_nil();
	cheax_ft(c, pad);

	/* we've leapt into a tail call */
	struct chx_list *was_last_call = c->bt.last_call;
	chx_ref last_call_ref = cheax_ref_ptr(c, was_last_call);
	chx_ref ret_last_call_ref = cheax_ref_ptr(c, ret_last_call);
	chx_ref ret_env_ref = cheax_ref_ptr(c, ret_env);

	struct chx_env *pop_stop;
	int tail_lvls = -1;

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
cheax_apply(CHEAX *c, struct chx_value func, struct chx_list *args)
{
	struct apply_func_info afi = { func, args };

	switch (func.type) {
	case CHEAX_SPECIAL_FORM:
	case CHEAX_SPECIAL_TAIL_FORM:
	case CHEAX_FUNC:
		return wrap_tail_eval(c, apply_func_evaluator, &afi);

	default:
		cheax_throwf(c, CHEAX_ETYPE, "apply(): only ExtFunc and Func allowed (got type %d)", func.type);
		return cheax_nil();
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
	case CHEAX_SPECIAL_FORM:
	case CHEAX_SPECIAL_TAIL_FORM:
		return (l.data.as_special_form->perform.func == r.data.as_special_form->perform.func)
		    && (l.data.as_special_form->info == r.data.as_special_form->info);
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
	return (0 == unpack(c, args, ".", &arg))
	     ? bt_wrap(c, cheax_eval(c, arg))
	     : cheax_nil();
}

static int
bltn_case(CHEAX *c,
          struct chx_list *args,
          void *info,
          struct chx_env *pop_stop,
          union chx_eval_out *out)
{
	if (args == NULL) {
		cheax_throwf(c, CHEAX_EMATCH, "invalid case");
		out->value = bt_wrap(c, cheax_nil());
		return CHEAX_VALUE_OUT;
	}

	struct chx_value what = cheax_eval(c, args->value);
	cheax_ft(c, pad);

	/* pattern-value-pair */
	for (struct chx_list *pvp = args->next; pvp != NULL; pvp = pvp->next) {
		struct chx_value pair = pvp->value;
		if (pair.type != CHEAX_LIST || pair.data.as_list == NULL) {
			cheax_throwf(c, CHEAX_EMATCH, "pattern-value pair expected");
			out->value = bt_wrap(c, cheax_nil());
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
	out->value = cheax_nil();
	return CHEAX_VALUE_OUT;
}

static int
bltn_apply(CHEAX *c,
           struct chx_list *args,
           void *info,
           struct chx_env *pop_stop,
           union chx_eval_out *out)
{
	struct chx_value func;
	struct chx_list *list;
	if (unpack(c, args, "[lpt]c", &func, &list) < 0) {
		out->value = cheax_nil();
		return CHEAX_VALUE_OUT;
	}

	int res = CHEAX_VALUE_OUT;
	struct apply_func_info afi = { func, list };

	switch (func.type) {
	case CHEAX_FUNC:
	case CHEAX_SPECIAL_FORM:
	case CHEAX_SPECIAL_TAIL_FORM:
		res = apply_func_evaluator(c, &afi, pop_stop, out);
		break;
	}

	if (cheax_errno(c) != 0)
		cheax_add_bt(c);

	return res;
}

static int
bltn_cond(CHEAX *c,
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
	out->value = cheax_nil();
	return CHEAX_VALUE_OUT;
}

static struct chx_value
bltn_eq(CHEAX *c, struct chx_list *args, void *info)
{
	struct chx_value l, r;
	return (0 == unpack(c, args, "..", &l, &r))
	     ? bt_wrap(c, cheax_bool(cheax_eq(c, l, r)))
	     : cheax_nil();
}

static struct chx_value
bltn_ne(CHEAX *c, struct chx_list *args, void *info)
{
	struct chx_value l, r;
	return (0 == unpack(c, args, "..", &l, &r))
	     ? bt_wrap(c, cheax_bool(!cheax_eq(c, l, r)))
	     : cheax_nil();
}

void
export_eval_bltns(CHEAX *c)
{
	cheax_def_special_form(c, "eval", bltn_eval, NULL);
	cheax_def_special_tail_form(c, "apply", bltn_apply, NULL);
	cheax_def_special_tail_form(c, "case",  bltn_case,  NULL);
	cheax_def_special_tail_form(c, "cond",  bltn_cond,  NULL);
	cheax_def_special_form(c, "=",    bltn_eq,   NULL);
	cheax_def_special_form(c, "!=",   bltn_ne,   NULL);
}
