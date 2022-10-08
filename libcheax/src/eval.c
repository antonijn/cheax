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

static struct chx_value
eval_func_call(CHEAX *c, struct chx_value fn_value, struct chx_list *args, bool argeval_override)
{
	struct chx_func *fn = fn_value.data.as_func;
	struct chx_value res = cheax_nil(), args_val = cheax_list_value(args);

	bool call_ok = false;
	int ty = fn_value.type;

	struct chx_env *prev_env = c->env;
	c->env = fn->lexenv;

	cheax_push_env(c);
	if (cheax_errno(c) != 0) {
		cheax_add_bt(c);
		goto env_fail_pad;
	}

	chx_ref prev_env_ref = cheax_ref_ptr(c, prev_env);

	int mflags = CHEAX_READONLY;
	if (ty == CHEAX_FUNC && !argeval_override)
		mflags |= CHEAX_EVAL_NODES;
	bool arg_match_ok = cheax_match_in(c, prev_env, fn->args, args_val, mflags);

	if (!arg_match_ok) {
		if (cheax_errno(c) == 0)
			cheax_throwf(c, CHEAX_EMATCH, "invalid (number of) arguments");
		cheax_add_bt(c);
		goto pad;
	}

	for (struct chx_list *cons = fn->body; cons != NULL; cons = cons->next) {
		res = bt_wrap(c, cheax_eval(c, cons->value));
		cheax_ft(c, pad);
	}

	call_ok = true;
pad:
	cheax_pop_env(c);
	cheax_unref_ptr(c, prev_env, prev_env_ref);
env_fail_pad:
	c->env = prev_env;
	if (call_ok && ty == CHEAX_MACRO)
		res = bt_wrap(c, cheax_eval(c, res));
	return res;
}

static struct chx_value
eval_env_call(CHEAX *c, struct chx_env *env, struct chx_list *args)
{
	cheax_enter_env(c, env);
	if (cheax_errno(c) != 0)
		return bt_wrap(c, cheax_nil());

	struct chx_value res = cheax_nil();
	for (struct chx_list *cons = args; cons != NULL; cons = cons->next) {
		res = bt_wrap(c, cheax_eval(c, cons->value));
		cheax_ft(c, env_pad);
	}
env_pad:
	cheax_pop_env(c);
	return res;
}

static struct chx_value
eval_cast(CHEAX *c, chx_int head, struct chx_list *args)
{
	struct chx_value cast_arg;
	return (0 == unpack(c, args, ".", &cast_arg))
	     ? bt_wrap(c, cheax_cast(c, cast_arg, head))
	     : cheax_nil();
}

static struct chx_value
eval_sexpr(CHEAX *c, struct chx_list *input)
{
	if (c->stack_limit > 0 && c->stack_depth >= c->stack_limit) {
		cheax_throwf(c, CHEAX_ESTACK, "stack overflow! (stack limit %d)", c->stack_limit);
		return cheax_nil();
	}

	struct chx_value res = cheax_nil();
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
		res = head.data.as_ext_func->perform(c, args, head.data.as_ext_func->info);
		break;

	case CHEAX_FUNC:
	case CHEAX_MACRO:
		res = eval_func_call(c, head, args, false);
		break;

	case CHEAX_TYPECODE:
		res = eval_cast(c, head.data.as_int, args);
		break;

	case CHEAX_ENV:
		res = eval_env_call(c, head.data.as_env, args);
		break;

	default:
		cheax_throwf(c, CHEAX_ETYPE, "invalid function call");
		cheax_add_bt(c);
		break;
	}

	cheax_unref(c, head, head_ref);
	c->bt.last_call = was_last_call;

	chx_ref res_ref = cheax_ref(c, res);
	cheax_gc(c);
	cheax_unref(c, res, res_ref);
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

struct chx_value
cheax_eval(CHEAX *c, struct chx_value input)
{
	struct chx_value res = cheax_nil();
	chx_ref input_ref;
	int code;

	switch (input.type) {
	case CHEAX_ID:
		return cheax_get(c, input.data.as_id->value);

	case CHEAX_LIST:
		return (input.data.as_list == NULL)
		     ? input
		     : eval_sexpr(c, input.data.as_list);

	case CHEAX_QUOTE:
		return input.data.as_quote->value;
	case CHEAX_BACKQUOTE:
		input_ref = cheax_ref(c, input);
		code = eval_bkquoted(c, &res, NULL, input.data.as_quote->value, 0);
		if (code == BKQ_SPLICED)
			cheax_throwf(c, CHEAX_EEVAL, "internal splice error");
		cheax_unref(c, input, input_ref);

		chx_ref res_ref = cheax_ref(c, res);
		cheax_gc(c);
		cheax_unref(c, res, res_ref);
		return res;

	case CHEAX_COMMA:
		cheax_throwf(c, CHEAX_EEVAL, "rogue comma");
		break;
	case CHEAX_SPLICE:
		cheax_throwf(c, CHEAX_EEVAL, "rogue ,@");
		break;
	}

	return input;
}

struct chx_value
cheax_apply(CHEAX *c, struct chx_value func, struct chx_list *args)
{
	switch (func.type) {
	case CHEAX_EXT_FUNC:
		return func.data.as_ext_func->perform(c, args, func.data.as_ext_func->info);

	case CHEAX_FUNC:
		return eval_func_call(c, func, args, true);

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

static struct chx_value
bltn_case(CHEAX *c, struct chx_list *args, void *info)
{
	if (args == NULL) {
		cheax_throwf(c, CHEAX_EMATCH, "invalid case");
		return bt_wrap(c, cheax_nil());
	}

	struct chx_value what = cheax_eval(c, args->value);
	cheax_ft(c, pad);

	/* pattern-value-pair */
	for (struct chx_list *pvp = args->next; pvp != NULL; pvp = pvp->next) {
		struct chx_value pair = pvp->value;
		if (pair.type != CHEAX_LIST || pair.data.as_list == NULL) {
			cheax_throwf(c, CHEAX_EMATCH, "pattern-value pair expected");
			return bt_wrap(c, cheax_nil());
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
		chx_ref what_ref = cheax_ref(c, what);

		struct chx_value retval = cheax_nil();
		for (struct chx_list *val = cons_pair->next; val != NULL; val = val->next) {
			retval = cheax_eval(c, val->value);
			cheax_ft(c, pad2);
		}

pad2:
		cheax_unref(c, what, what_ref);
		cheax_pop_env(c);
		return retval;
	}

	cheax_throwf(c, CHEAX_EMATCH, "non-exhaustive pattern");
	cheax_add_bt(c);
pad:
	return cheax_nil();
}

static struct chx_value
bltn_apply(CHEAX *c, struct chx_list *args, void *info)
{
	struct chx_value func;
	struct chx_list *list;
	return (0 == unpack(c, args, "[lp]c", &func, &list))
	     ? bt_wrap(c, cheax_apply(c, func, list))
	     : cheax_nil();
}

static struct chx_value
bltn_cond(CHEAX *c, struct chx_list *args, void *info)
{
	/* test-value-pair */
	for (struct chx_list *tvp = args; tvp != NULL; tvp = tvp->next) {
		struct chx_value pair = tvp->value;
		if (pair.type != CHEAX_LIST || pair.data.as_list == NULL) {
			cheax_throwf(c, CHEAX_EMATCH, "test-value pair expected");
			return bt_wrap(c, cheax_nil());
		}

		struct chx_list *cons_pair = pair.data.as_list;
		struct chx_value test = cons_pair->value, retval = cheax_nil();

		cheax_push_env(c);
		cheax_ft(c, pad);

		test = cheax_eval(c, test);
		cheax_ft(c, pad2);
		if (test.type != CHEAX_BOOL) {
			cheax_throwf(c, CHEAX_ETYPE, "test must have boolean value");
			cheax_add_bt(c);
			goto pad2;
		}

		if (test.data.as_int == 0) {
			cheax_pop_env(c);
			continue;
		}

		/* condition met! */
		for (struct chx_list *val = cons_pair->next; val != NULL; val = val->next) {
			retval = cheax_eval(c, val->value);
			cheax_ft(c, pad2);
		}

pad2:
		cheax_pop_env(c);
		return retval;
	}

pad:
	return cheax_nil();
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
	cheax_defmacro(c, "eval",  bltn_eval,  NULL);
	cheax_defmacro(c, "apply", bltn_apply, NULL);
	cheax_defmacro(c, "case",  bltn_case,  NULL);
	cheax_defmacro(c, "cond",  bltn_cond,  NULL);
	cheax_defmacro(c, "=",     bltn_eq,    NULL);
	cheax_defmacro(c, "!=",    bltn_ne,    NULL);
}
