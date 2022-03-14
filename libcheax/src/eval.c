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

void
cheax_exec(CHEAX *c, const char *path)
{
	if (path == NULL) {
		cheax_throwf(c, CHEAX_EAPI, "exec(): `path' cannot be NULL");
		return;
	}

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

	struct chx_value *v;
	while ((v = cheax_read_at(c, f, path, &line, &pos)) != NULL) {
		cheax_ft(c, pad);
		cheax_eval(c, v);
		cheax_ft(c, pad);
	}

pad:
	fclose(f);
}

static struct chx_value *
eval_sexpr(CHEAX *c, struct chx_list *input)
{
	struct chx_value *res = NULL, *head = cheax_eval(c, input->value);
	chx_ref head_ref = cheax_ref(c, head);
	cheax_ft(c, pad);

	struct chx_list *args = input->next;

	struct chx_value *cast_arg;
	struct chx_ext_func *extf;
	struct chx_func *fn;
	struct chx_env *env;
	struct chx_env *new_env;

	int ty = cheax_type_of(head);
	switch (ty) {
	case CHEAX_EXT_FUNC:
		extf = (struct chx_ext_func *)head;
		res = extf->perform(c, input->next, extf->info);
		break;

	case CHEAX_FUNC:
	case CHEAX_MACRO:
		fn = (struct chx_func *)head;
		bool call_ok = false;

		struct chx_env *prev_env = c->env;
		chx_ref prev_env_ref = cheax_ref(c, prev_env);
		c->env = fn->lexenv;

		new_env = cheax_push_env(c);
		if (new_env == NULL) {
			cheax_add_bt(c);
			goto env_fail_pad;
		}
		/* probably won't escape; major memory optimisation */
		new_env->base.rtflags |= NO_ESC_BIT;

		int mflags = CHEAX_READONLY | ((ty == CHEAX_FUNC) ? CHEAX_EVAL_NODES : 0);
		bool arg_match_ok = cheax_match_in(c, prev_env, fn->args, &args->base, mflags);

		if (!arg_match_ok) {
			if (cheax_errno(c) == 0)
				cheax_throwf(c, CHEAX_EMATCH, "invalid (number of) arguments");
			cheax_add_bt(c);
			goto fn_pad;
		}

		for (struct chx_list *cons = fn->body; cons != NULL; cons = cons->next) {
			res = bt_wrap(c, cheax_eval(c, cons->value));
			cheax_ft(c, fn_pad);
		}

		call_ok = true;
fn_pad:
		cheax_pop_env(c);
env_fail_pad:
		cheax_unref(c, prev_env, prev_env_ref);
		c->env = prev_env;
		if (call_ok && ty == CHEAX_MACRO)
			res = bt_wrap(c, cheax_eval(c, res));
		break;

	case CHEAX_TYPECODE:
		if (0 == unpack(c, args, ".", &cast_arg))
			res = bt_wrap(c, cheax_cast(c, cast_arg, ((struct chx_int *)head)->value));
		break;

	case CHEAX_ENV:
		env = (struct chx_env *)head;
		new_env = cheax_enter_env(c, env);
		if (new_env == NULL) {
			cheax_add_bt(c);
			break;
		}
		/* probably won't escape; major memory optimisation */
		new_env->base.rtflags |= NO_ESC_BIT;

		for (struct chx_list *cons = args; cons != NULL; cons = cons->next) {
			res = bt_wrap(c, cheax_eval(c, cons->value));
			cheax_ft(c, env_pad);
		}
env_pad:
		cheax_pop_env(c);
		break;

	default:
		cheax_throwf(c, CHEAX_ETYPE, "eval(): invalid function call");
		cheax_add_bt(c);
		break;
	}

pad:
	cheax_unref(c, head, head_ref);
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
		cheax_ft(c, pad);
		chx_ref car_ref = cheax_ref(c, car);
		struct chx_value *cdr = eval_bkquoted(c, &lst_quoted->next->base, nest);
		cheax_unref(c, car, car_ref);
		cheax_ft(c, pad);

		if (has_flag(quoted->rtflags, DEBUG_LIST)) {
			struct debug_info info = ((struct debug_list *)quoted)->info;
			res = &debug_list(c, car, (struct chx_list *)cdr, info)->base.base;
		} else {
			res = &cheax_list(c, car, (struct chx_list *)cdr)->base;
		}
		break;

	case CHEAX_QUOTE:
		qt_quoted = (struct chx_quote *)quoted;
		struct chx_value *to_quote = eval_bkquoted(c, qt_quoted->value, nest);
		cheax_ft(c, pad);
		res = &cheax_quote(c, to_quote)->base;
		break;
	case CHEAX_BACKQUOTE:
		qt_quoted = (struct chx_quote *)quoted;
		struct chx_value *to_bkquote = eval_bkquoted(c, qt_quoted->value, nest + 1);
		cheax_ft(c, pad);
		res = &cheax_backquote(c, to_bkquote)->base;
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
	chx_ref input_ref = cheax_ref(c, input);
	int prev_stack_depth;
	struct chx_list *was_last_call;

	switch (cheax_type_of(input)) {
	case CHEAX_ID:
		res = cheax_get(c, ((struct chx_id *)input)->id);
		break;

	case CHEAX_LIST:
		prev_stack_depth = c->stack_depth++;
		was_last_call = c->bt.last_call;
		c->bt.last_call = (struct chx_list *)input;

		if (c->stack_limit > 0 && prev_stack_depth >= c->stack_limit) {
			cheax_throwf(c, CHEAX_ESTACK, "eval(): stack overflow! (stack limit %d)", c->stack_limit);
			cheax_add_bt(c);
		} else {
			res = eval_sexpr(c, (struct chx_list *)input);
		}

		c->stack_depth = prev_stack_depth;
		c->bt.last_call = was_last_call;

		break;

	case CHEAX_QUOTE:
		res = ((struct chx_quote *)input)->value;
		break;
	case CHEAX_BACKQUOTE:
		res = eval_bkquoted(c, ((struct chx_quote *)input)->value, 0);
		break;

	case CHEAX_COMMA:
		cheax_throwf(c, CHEAX_EEVAL, "eval(): rogue comma");
		break;

	default:
		res = input;
		break;
	}

	cheax_unref(c, input, input_ref);

	chx_ref res_ref = cheax_ref(c, res);
	cheax_gc(c);
	cheax_unref(c, res, res_ref);
	return res;
}

static bool
match_node(CHEAX *c,
           struct chx_env *env,
           struct chx_value *pan_node,
           struct chx_value *match_node,
           int flags)
{
	if (has_flag(flags, CHEAX_EVAL_NODES)) {
		struct chx_env *prev_env = c->env;
		chx_ref prev_env_ref = cheax_ref(c, prev_env);
		c->env = env;

		match_node = cheax_eval(c, match_node);

		cheax_unref(c, prev_env, prev_env_ref);
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
		return cheax_match_in(c, env, pan->value, &match->base, flags);

	return match != NULL
	    && match_node(c, env, pan->value, match->value, flags)
	    && match_colon(c, env, pan->next, match->next, flags);
}

static bool
match_list(CHEAX *c,
           struct chx_env *env,
           struct chx_list *pan,
           struct chx_list *match,
           int flags)
{
	if (cheax_type_of(pan->value) == CHEAX_ID
	 && strcmp((((struct chx_id *)pan->value)->id), ":") == 0)
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
               struct chx_value *pan,
               struct chx_value *match,
               int flags)
{
	int pan_ty = cheax_type_of(pan);

	if (pan_ty == CHEAX_ID) {
		if (has_flag(flags, CHEAX_EVAL_NODES)
		 && (cheax_type_of(match) == CHEAX_LIST || match == NULL))
		{
			struct chx_env *prev_env = c->env;
			chx_ref prev_env_ref = cheax_ref(c, prev_env);
			c->env = env;

			int res = unpack(c, (struct chx_list *)match, ".*", &match);

			cheax_unref(c, prev_env, prev_env_ref);
			c->env = prev_env;
			if (res < 0)
				return false;
		}

		cheax_def(c, ((struct chx_id *)pan)->id, match, flags);
		return cheax_errno(c) == 0; /* false if cheax_def() failed */
	}

	if (pan_ty != cheax_type_of(match))
		return false;

	switch (pan_ty) {
	case CHEAX_LIST:
		return match_list(c, env, (struct chx_list *)pan, (struct chx_list *)match, flags);
	case CHEAX_NIL:
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
cheax_match(CHEAX *c, struct chx_value *pan, struct chx_value *match, int flags)
{
	return cheax_match_in(c, c->env, pan, match, flags);
}

bool
cheax_eq(CHEAX *c, struct chx_value *l, struct chx_value *r)
{
	if (cheax_type_of(l) != cheax_type_of(r))
		return false;

	switch (cheax_resolve_type(c, cheax_type_of(l))) {
	case CHEAX_NIL:
		return true;
	case CHEAX_ID:
		return strcmp(((struct chx_id *)l)->id, ((struct chx_id *)r)->id) == 0;
	case CHEAX_INT:
	case CHEAX_BOOL:
		return ((struct chx_int *)l)->value == ((struct chx_int *)r)->value;
	case CHEAX_DOUBLE:
		return ((struct chx_double *)l)->value == ((struct chx_double *)r)->value;
	case CHEAX_LIST:
		;
		struct chx_list *llist = (struct chx_list *)l;
		struct chx_list *rlist = (struct chx_list *)r;
		return cheax_eq(c, llist->value, rlist->value)
		    && cheax_eq(c, &llist->next->base, &rlist->next->base);
	case CHEAX_EXT_FUNC:
		;
		struct chx_ext_func *extfl = (struct chx_ext_func *)l;
		struct chx_ext_func *extfr = (struct chx_ext_func *)r;
		return extfl->perform == extfr->perform && extfl->info == extfr->info;
	case CHEAX_QUOTE:
	case CHEAX_BACKQUOTE:
	case CHEAX_COMMA:
		return cheax_eq(c, ((struct chx_quote *)l)->value, ((struct chx_quote *)r)->value);
	case CHEAX_STRING:
		;
		struct chx_string *lstring = (struct chx_string *)l;
		struct chx_string *rstring = (struct chx_string *)r;
		return (lstring->len == rstring->len)
		    && memcmp(lstring->value, rstring->value, lstring->len) == 0;
	case CHEAX_USER_PTR:
		return ((struct chx_user_ptr *)l)->value == ((struct chx_user_ptr *)r)->value;
	default:
		return l == r;
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

static struct chx_value *
bltn_eval(CHEAX *c, struct chx_list *args, void *info)
{
	struct chx_value *arg;
	return (0 == unpack(c, args, ".", &arg))
	     ? bt_wrap(c, cheax_eval(c, arg))
	     : NULL;
}

static struct chx_value *
bltn_case(CHEAX *c, struct chx_list *args, void *info)
{
	if (args == NULL) {
		cheax_throwf(c, CHEAX_EMATCH, "invalid case");
		return bt_wrap(c, NULL);
	}

	struct chx_value *what = cheax_eval(c, args->value);
	cheax_ft(c, pad);

	/* pattern-value-pair */
	for (struct chx_list *pvp = args->next; pvp; pvp = pvp->next) {
		struct chx_value *pair = pvp->value;
		if (cheax_type_of(pair) != CHEAX_LIST) {
			cheax_throwf(c, CHEAX_EMATCH, "pattern-value pair expected");
			return bt_wrap(c, NULL);
		}

		struct chx_list *cons_pair = (struct chx_list *)pair;
		struct chx_value *pan = cons_pair->value;

		if (cheax_push_env(c) == NULL) {
			cheax_add_bt(c);
			goto pad;
		}

		if (!cheax_match(c, pan, what, CHEAX_READONLY)) {
			cheax_pop_env(c);
			continue;
		}

		/* pattern matches! */
		chx_ref what_ref = cheax_ref(c, what);

		struct chx_value *retval = NULL;
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
	return NULL;
}

static struct chx_value *
bltn_eq(CHEAX *c, struct chx_list *args, void *info)
{
	struct chx_value *l, *r;
	return (0 == unpack(c, args, "..", &l, &r))
	     ? bt_wrap(c, &cheax_bool(c, cheax_eq(c, l, r))->base)
	     : NULL;
}

static struct chx_value *
bltn_ne(CHEAX *c, struct chx_list *args, void *info)
{
	struct chx_value *l, *r;
	return (0 == unpack(c, args, "..", &l, &r))
	     ? bt_wrap(c, &cheax_bool(c, !cheax_eq(c, l, r))->base)
	     : NULL;
}

void
export_eval_bltns(CHEAX *c)
{
	cheax_defmacro(c, "eval", bltn_eval, NULL);
	cheax_defmacro(c, "case", bltn_case, NULL);
	cheax_defmacro(c, "=",    bltn_eq,   NULL);
	cheax_defmacro(c, "!=",   bltn_ne,   NULL);
}
