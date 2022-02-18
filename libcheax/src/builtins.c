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
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "api.h"
#include "gc.h"

/* Calls cheax_ref() on all unpacked args, doesn't unref() on failure.
 * Returns the number of arguments unpacked. */
static int
vunpack_args(CHEAX *c,
             const char *fname,
             struct chx_list *args,
             bool eval_args,
             int num,
             va_list ap)
{
	int i;
	for (i = 0; i < num; ++i) {
		if (args == NULL) {
			cry(c, fname, CHEAX_EMATCH, "expected %d arguments (got %d)", num, i);
			return i;
		}

		struct chx_value **res = va_arg(ap, struct chx_value **);
		if (eval_args) {
			*res = cheax_eval(c, args->value);
			cheax_ref(c, *res);
			cheax_ft(c, pad);
		} else {
			*res = args->value;
		}
		args = args->next;
	}

	if (args != NULL) {
		cry(c, fname, CHEAX_EMATCH, "expected only %d arguments", num);
		return i;
	}

pad:
	return i;
}

/* Does not cheax_ref() any unpacked arguments */
static bool
unpack_args(CHEAX *c,
            const char *fname,
            struct chx_list *args,
            bool eval_args,
            int num, ...)
{
	va_list ap;
	va_start(ap, num);
	int res = vunpack_args(c, fname, args, eval_args, num, ap);
	va_end(ap);

	va_start(ap, num);
	for (int i = 0; i < res; ++i)
		cheax_unref(c, *va_arg(ap, struct chx_value **));
	va_end(ap);

	cheax_ft(c, pad);

	return true;

pad:
	return false;
}


static struct chx_value *
builtin_cheax_version(CHEAX *c, struct chx_list *args, void *info)
{
	if (!unpack_args(c, "cheax-version", args, false, 0))
		return NULL;

	return &cheax_string(c, cheax_version())->base;
}

static struct chx_value *
builtin_fopen(CHEAX *c, struct chx_list *args, void *info)
{
	struct chx_value *fname, *mode;
	if (!unpack_args(c, "fopen", args, true, 2, &fname, &mode))
		return NULL;

	if (cheax_type_of(fname) != CHEAX_STRING || cheax_type_of(mode) != CHEAX_STRING) {
		cry(c, "fopen", CHEAX_ETYPE, "invalid argument type");
		return NULL;
	}

	const char *fname_str = ((struct chx_string *)fname)->value;
	const char *mode_str = ((struct chx_string *)mode)->value;
	FILE *f = fopen(fname_str, mode_str);

	return &cheax_user_ptr(c, f, c->fhandle_type)->base;
}
static struct chx_value *
builtin_fclose(CHEAX *c, struct chx_list *args, void *info)
{
	struct chx_value *handle;
	if (!unpack_args(c, "fclose", args, true, 1, &handle))
		return NULL;

	if (cheax_type_of(handle) != c->fhandle_type) {
		cry(c, "fclose", CHEAX_ETYPE, "expected file handle");
		return NULL;
	}

	FILE *f = (FILE *)((struct chx_user_ptr *)handle)->value;
	fclose(f);

	return NULL;
}
static struct chx_value *
builtin_read_from(CHEAX *c, struct chx_list *args, void *info)
{
	struct chx_value *handle;
	if (!unpack_args(c, "read-from", args, true, 1, &handle))
		return NULL;

	if (cheax_type_of(handle) != c->fhandle_type) {
		cry(c, "fclose", CHEAX_ETYPE, "expected file handle");
		return NULL;
	}

	FILE *f = (FILE *)((struct chx_user_ptr *)handle)->value;
	return cheax_read(c, f);
}
static struct chx_value *
builtin_print_to(CHEAX *c, struct chx_list *args, void *info)
{
	struct chx_value *handle, *value;
	if (!unpack_args(c, "print-to", args, true, 2, &handle, &value))
		return NULL;

	if (cheax_type_of(handle) != c->fhandle_type) {
		cry(c, "print-to", CHEAX_ETYPE, "expected file handle");
		return NULL;
	}

	FILE *f = (FILE *)((struct chx_user_ptr *)handle)->value;
	cheax_print(c, f, value);
	fputc('\n', f);
	return NULL;
}
static struct chx_value *
builtin_put_to(CHEAX *c, struct chx_list *args, void *info)
{
	struct chx_value *handle, *value;
	if (!unpack_args(c, "put-to", args, true, 2, &handle, &value))
		return NULL;

	if (cheax_type_of(handle) != c->fhandle_type) {
		cry(c, "put-to", CHEAX_ETYPE, "expect file handle");
		return NULL;
	}

	if (cheax_type_of(value) != CHEAX_STRING) {
		cry(c, "put-to", CHEAX_ETYPE, "expected string");
		return NULL;
	}

	FILE *f = (FILE *)((struct chx_user_ptr *)handle)->value;
	struct chx_string *s = (struct chx_string *)value;
	fwrite(s->value, 1, s->len, f);
	return NULL;
}

static struct chx_value *
builtin_format(CHEAX *c, struct chx_list *args, void *info)
{
	/* evaluate arguments */
	struct chx_list *ev_args = NULL;
	struct chx_list **ev_args_last = &ev_args;
	for (struct chx_list *arg = args; arg != NULL; arg = arg->next) {
		struct chx_value *arge = cheax_eval(c, arg->value);

		/* won't set if ev_args is NULL, so this ensures
		 * the GC won't delete our argument list */
		cheax_unref(c, ev_args);

		cheax_ft(c, pad);

		*ev_args_last = cheax_list(c, arge, NULL);
		ev_args_last = &(*ev_args_last)->next;

		cheax_ref(c, ev_args);
	}

	cheax_unref(c, ev_args);

	if (ev_args == NULL) {
		cry(c, "format", CHEAX_EMATCH, "expected at least 1 argument (got 0)");
		return NULL;
	}

	struct chx_value *fmt_str_val = ev_args->value;

	if (cheax_type_of(fmt_str_val) != CHEAX_STRING) {
		cry(c, "format", CHEAX_ETYPE, "expected format string");
		return NULL;
	}

	struct chx_string *fmt_str = (struct chx_string *)fmt_str_val;
	return cheax_format(c, fmt_str->value, ev_args->next);

pad:
	return NULL;
}

static struct chx_value *
builtin_bytes(CHEAX *c, struct chx_list *args, void *info)
{
	struct chx_value *arg;
	if (!unpack_args(c, "bytes", args, true, 1, &arg))
		return NULL;

	if (cheax_type_of(arg) != CHEAX_STRING) {
		cry(c, "bytes", CHEAX_ETYPE, "expected string");
		return NULL;
	}

	struct chx_string *str = (struct chx_string *)arg;
	struct chx_list *bytes = NULL;

	for (int i = (int)str->len - 1; i >= 0; --i)
		bytes = cheax_list(c, &cheax_int(c, str->value[i])->base, bytes);

	return &bytes->base;
}

static struct chx_value *
builtin_def(CHEAX *c, struct chx_list *args, void *info)
{
	struct chx_value *idval, *setto;
	if (!unpack_args(c, "def", args, false, 2, &idval, &setto))
		return NULL;

	setto = cheax_eval(c, setto);
	cheax_ft(c, pad);

	if (!cheax_match(c, idval, setto, CHEAX_READONLY)) {
		cry(c, "def", CHEAX_EMATCH, "invalid pattern");
		return NULL;
	}

pad:
	return NULL;
}

static struct chx_value *
builtin_error_code(CHEAX *c, struct chx_list *args, void *info)
{
	if (!unpack_args(c, "error-code", args, false, 0))
		return NULL;

	struct chx_value *res = &cheax_int(c, c->error.code)->base;
	set_type(res, CHEAX_ERRORCODE);
	return res;
}
static struct chx_value *
builtin_error_msg(CHEAX *c, struct chx_list *args, void *info)
{
	if (!unpack_args(c, "error-msg", args, false, 0))
		return NULL;

	return &c->error.msg->base;
}
static struct chx_value *
builtin_throw(CHEAX *c, struct chx_list *args, void *info)
{
	struct chx_value *code, *msg;
	if (!unpack_args(c, "throw", args, true, 2, &code, &msg))
		return NULL;

	if (cheax_type_of(code) != CHEAX_ERRORCODE) {
		cry(c, "throw", CHEAX_ETYPE, "expected error code");
		return NULL;
	}

	if (((struct chx_int *)code)->value == 0) {
		cry(c, "throw", CHEAX_EVALUE, "cannot throw error code 0");
		return NULL;
	}

	if (msg != NULL && cheax_type_of(msg) != CHEAX_STRING) {
		cry(c, "throw", CHEAX_ETYPE, "expected string message");
		return NULL;
	}

	cheax_throw(c, ((struct chx_int *)code)->value, (struct chx_string *)msg);
	return NULL;
}
static struct chx_value *
builtin_try(CHEAX *c, struct chx_list *args, void *info)
{
	if (args == NULL) {
		cry(c, "try", CHEAX_EMATCH, "expected at least two arguments");
		return NULL;
	}

	struct chx_value *block = args->value;
	struct chx_list *catch_blocks = args->next;
	if (catch_blocks == NULL) {
		cry(c, "try", CHEAX_EMATCH, "expected at least one catch/finally block");
		return NULL;
	}

	/* The item such that for the final catch block cb, we have
	 * cb->next == finally_block */
	struct chx_list *finally_block = NULL;

	for (struct chx_list *cb = catch_blocks; cb != NULL; cb = cb->next) {
		struct chx_value *cb_value = cb->value;
		if (cheax_type_of(cb_value) != CHEAX_LIST) {
			cry(c, "try", CHEAX_ETYPE, "catch/finally blocks must be s-expressions");
			return NULL;
		}

		struct chx_list *cb_list = (struct chx_list *)cb_value;
		bool is_id = cheax_type_of(cb_list->value) == CHEAX_ID;

		struct chx_id *keyword = (struct chx_id *)cb_list->value;
		if (is_id && 0 == strcmp("catch", keyword->id)) {
			if (cb_list->next == NULL || cb_list->next->next == NULL) {
				cry(c, "catch", CHEAX_EMATCH, "expected at least two arguments");
				return NULL;
			}
		} else if (is_id && 0 == strcmp("finally", keyword->id)) {
			if (cb->next != NULL) {
				cry(c, "finally", CHEAX_EVALUE, "unexpected values after finally block");
				return NULL;
			}

			finally_block = cb;
		} else {
			cry(c, "try", CHEAX_EMATCH, "expected `catch' or `finally' keyword");
			return NULL;
		}
	}

	struct chx_value *retval = cheax_eval(c, block);
	if (cheax_errstate(c) == CHEAX_RUNNING)
		goto run_finally;

	/* error caught */
	c->error.state = CHEAX_RUNNING;

	/* catch block that matches */
	struct chx_list *match = NULL;

	for (struct chx_list *cb = catch_blocks;
	     match == NULL && cb != finally_block;
	     cb = cb->next)
	{
		/* these type casts should be safe, we did these checks
		 * beforehand */
		struct chx_list *cb_list = (struct chx_list *)cb->value;

		/* two example catch blocks:
		 *   (catch EVALUE ...)
		 *   (catch (list EMATCH ENIL) ...)
		 * the former matches only EVALUE, the latter both EMATCH
		 * and ENIL.
		 *
		 * cb_list->value       should be the keyword `catch'
		 * cb_list->next->value is the error code to match against
		 * cb_list->next->next  is the list of code blocks to run
		 *                      if we have a match.
		 */
		struct chx_value *errcodes = cheax_eval(c, cb_list->next->value);
		cheax_ft(c, run_finally);

		if (cheax_type_of(errcodes) != CHEAX_LIST)
			errcodes = &cheax_list(c, errcodes, NULL)->base;

		for (struct chx_list *enode = (struct chx_list *)errcodes; enode != NULL; enode = enode->next) {
			struct chx_value *code = enode->value;
			if (cheax_type_of(code) != CHEAX_ERRORCODE) {
				cry(c, "catch", CHEAX_ETYPE, "expected error code or list thereof");
				goto run_finally;
			}

			if (((struct chx_int *)code)->value == cheax_errno(c))
				match = cb_list;
		}
	}

	if (match == NULL) {
		/* error falls through */
		c->error.state = CHEAX_THROWN;
	} else {
		/* match, so run catch block code */
		struct chx_list *run_blocks = match->next->next;
		cheax_ref(c, run_blocks);

		c->error.state = CHEAX_RUNNING;

		for (struct chx_list *cons = run_blocks; cons != NULL; cons = cons->next) {
			retval = cheax_eval(c, cons->value);
			cheax_ft(c, pad1); /* new error thrown */
		}

		cheax_clear_errno(c); /* error taken care of */
pad1:
		cheax_unref(c, run_blocks);
	}

run_finally:
	if (finally_block != NULL) {
		cheax_ref(c, retval);

		int prev_errstate = c->error.state;
		c->error.state = CHEAX_RUNNING;

		/* types checked before, so this should all be safe */
		struct chx_list *fb = (struct chx_list *)finally_block->value;
		for (struct chx_list *cons = fb->next; cons != NULL; cons = cons->next) {
			cheax_eval(c, cons->value);
			cheax_ft(c, pad2);
		}

		c->error.state = prev_errstate;
pad2:
		cheax_unref(c, retval);
	}

	return retval;
}
static struct chx_value *
builtin_new_error_code(CHEAX *c, struct chx_list *args, void *info)
{
	struct chx_value *errname_id;
	if (!unpack_args(c, "new-error-code", args, false, 1, &errname_id))
		return NULL;

	if (cheax_type_of(errname_id) != CHEAX_ID) {
		cry(c, "new-error-code", CHEAX_ETYPE, "expected ID");
		return NULL;
	}

	const char *errname = ((struct chx_id *)errname_id)->id;
	cheax_new_error_code(c, errname);

	return NULL;
}

static struct chx_value *
builtin_exit(CHEAX *c, struct chx_list *args, void *info)
{
	int code = 0;

	if (args != NULL) {
		struct chx_value *code_val;
		if (!unpack_args(c, "exit", args, false, 1, &code_val))
			return NULL;

		if (cheax_type_of(code_val) != CHEAX_INT) {
			cry(c, "exit", CHEAX_ETYPE, "expected integer exit code");
			return NULL;
		}

		code = ((struct chx_int *)code_val)->value;
	}

	exit(code);
}

struct defsym_info {
	struct chx_func *get, *set;
};

static void
defgetset(CHEAX *c, const char *name,
          struct chx_value *getset_args,
          struct chx_list *args,
          struct defsym_info *info,
          struct chx_func **out)
{
	if (info == NULL) {
		cry(c, name, CHEAX_EEVAL, "out of symbol scope");
		return;
	}

	if (args == NULL) {
		cry(c, name, CHEAX_EMATCH, "expected body");
		return;
	}

	if (*out != NULL) {
		cry(c, name, CHEAX_EEXIST, "already called");
		return;
	}

	struct chx_func *res = cheax_alloc(c, sizeof(struct chx_func), CHEAX_FUNC);
	res->args = getset_args;
	res->body = args;
	res->lexenv = c->env;
	*out = res;
}

static struct chx_value *
builtin_defget(CHEAX *c, struct chx_list *args, void *info)
{
	struct defsym_info *dinfo = info;
	defgetset(c, "defget", NULL, args, dinfo, &dinfo->get);
	return NULL;
}
static struct chx_value *
builtin_defset(CHEAX *c, struct chx_list *args, void *info)
{
	static struct chx_id value_id = { { CHEAX_ID | NO_GC_BIT }, "value" };
	static struct chx_list set_args = { { CHEAX_LIST | NO_GC_BIT }, &value_id.base, NULL };

	struct defsym_info *dinfo = info;
	defgetset(c, "defset", &set_args.base, args, dinfo, &dinfo->set);
	return NULL;
}

static struct chx_value *
defsym_get(CHEAX *c, struct chx_sym *sym)
{
	struct defsym_info *info = sym->user_info;
	return cheax_eval(c, &cheax_list(c, &info->get->base, NULL)->base);
}
static void
defsym_set(CHEAX *c, struct chx_sym *sym, struct chx_value *value)
{
	struct defsym_info *info = sym->user_info;
	cheax_eval(c, &cheax_list(c, &info->set->base, cheax_list(c, value, NULL))->base);
}
static void
defsym_finalizer(CHEAX *c, struct chx_sym *sym)
{
	free(sym->user_info);
}

static struct chx_value *
builtin_defsym(CHEAX *c, struct chx_list *args, void *info)
{
	if (args == NULL) {
		cry(c, "defsym", CHEAX_EMATCH, "expected symbol name");
		return NULL;
	}

	struct chx_value *idval = args->value;
	if (cheax_type_of(idval) != CHEAX_ID) {
		cry(c, "defsym", CHEAX_ETYPE, "expected identifier");
		return NULL;
	}

	struct chx_id *id = (struct chx_id *)idval;
	bool body_ok = false;

	cheax_push_env(c);

	struct defsym_info *dinfo = malloc(sizeof(struct defsym_info));
	dinfo->get = dinfo->set = NULL;

	struct chx_ext_func *defget, *defset;
	defget = cheax_ext_func(c, "defget", builtin_defget, dinfo);
	defset = cheax_ext_func(c, "defset", builtin_defset, dinfo);

	cheax_var(c, "defget", &defget->base, CHEAX_READONLY);
	cheax_var(c, "defset", &defset->base, CHEAX_READONLY);

	for (struct chx_list *cons = args->next; cons != NULL; cons = cons->next) {
		cheax_eval(c, cons->value);
		cheax_ft(c, pad);
	}

	body_ok = true;
pad:
	defget->info = defset->info = NULL;
	cheax_pop_env(c);

	if (!body_ok)
		goto err_pad;

	if (dinfo->get == NULL && dinfo->set == NULL) {
		cry(c, "defsym", CHEAX_ENOSYM, "symbol must have getter or setter");
		goto err_pad;
	}

	chx_getter act_get = (dinfo->get == NULL) ? NULL : defsym_get;
	chx_setter act_set = (dinfo->set == NULL) ? NULL : defsym_set;
	struct chx_sym *sym = cheax_defsym(c, id->id, act_get, act_set, defsym_finalizer, dinfo);
	if (sym == NULL)
		goto err_pad;

	struct chx_list *protect = NULL;
	if (dinfo->get != NULL)
		protect = cheax_list(c, &dinfo->get->base, protect);
	if (dinfo->set != NULL)
		protect = cheax_list(c, &dinfo->set->base, protect);
	sym->protect = &protect->base;

	return NULL;
err_pad:
	free(dinfo);
	return NULL;
}

static struct chx_value *
builtin_var(CHEAX *c, struct chx_list *args, void *info)
{
	struct chx_value *idval, *setto = NULL;
	if (!((args && args->next && unpack_args(c, "var", args, false, 2, &idval, &setto))
	 || unpack_args(c, "var", args, false, 1, &idval)))
	{
		return NULL;
	}

	setto = cheax_eval(c, setto);
	cheax_ft(c, pad);

	if (!cheax_match(c, idval, setto, 0)) {
		cry(c, "var", CHEAX_EMATCH, "invalid pattern");
		return NULL;
	}

pad:
	return NULL;
}

static struct chx_value *
builtin_set(CHEAX *c, struct chx_list *args, void *info)
{
	struct chx_value *idval, *setto;
	if (!unpack_args(c, "set", args, false, 2, &idval, &setto))
		return NULL;

	if (cheax_type_of(idval) != CHEAX_ID) {
		cry(c, "set", CHEAX_ETYPE, "expected identifier");
		return NULL;
	}

	setto = cheax_eval(c, setto);
	cheax_ft(c, pad);
	cheax_set(c, ((struct chx_id *)idval)->id, setto);

pad:
	return NULL;
}

static struct chx_value *
builtin_env(CHEAX *c, struct chx_list *args, void *info)
{
	if (unpack_args(c, "env", args, false, 0))
		return &c->env->base;

	return NULL;
}

static struct chx_list *
prepend(CHEAX *c, struct chx_list *args)
{
	if (args->next != NULL) {
		struct chx_value *head = cheax_eval(c, args->value);
		cheax_ft(c, pad);
		cheax_ref(c, head);
		struct chx_list *tail = prepend(c, args->next);
		cheax_unref(c, head);
		cheax_ft(c, pad);
		return cheax_list(c, head, tail);
	}

	struct chx_value *res = cheax_eval(c, args->value);
	cheax_ft(c, pad);
	int ty = cheax_type_of(res);
	if (ty != CHEAX_LIST && ty != CHEAX_NIL) {
		cry(c, ":", CHEAX_ETYPE, "improper list not allowed");
		return NULL;
	}

	return (struct chx_list *)res;
pad:
	return NULL;
}
static struct chx_value *
builtin_prepend(CHEAX *c, struct chx_list *args, void *info)
{
	if (args == NULL) {
		cry(c, ":", CHEAX_EMATCH, "expected at least one argument");
		return NULL;
	}

	return &prepend(c, args)->base;
}

static struct chx_value *
builtin_gc(CHEAX *c, struct chx_list *args, void *info)
{
	if (unpack_args(c, "gc", args, false, 0))
		cheax_force_gc(c);

	return NULL;
}
static struct chx_value *
builtin_get_used_memory(CHEAX *c, struct chx_list *args, void *info)
{
	if (!unpack_args(c, "get-used-memory", args, false, 0))
		return NULL;

#ifdef USE_BOEHM_GC
	return &cheax_int(c, 0)->base;
#else
	return &cheax_int(c, c->gc.all_mem)->base;
#endif
}

static struct chx_value *
builtin_type_of(CHEAX *c, struct chx_list *args, void *info)
{
	struct chx_value *val;
	if (!unpack_args(c, "type-of", args, true, 1, &val))
		return NULL;

	struct chx_value *res = &cheax_int(c, cheax_type_of(val))->base;
	set_type(res, CHEAX_TYPECODE);
	return res;
}

static struct chx_value *
builtin_get_max_stack_depth(CHEAX *c, struct chx_list *args, void *info)
{
	if (!unpack_args(c, "get-max-stack-depth", args, false, 0))
		return NULL;

	return &cheax_int(c, c->max_stack_depth)->base;
}

static struct chx_value *
builtin_set_max_stack_depth(CHEAX *c, struct chx_list *args, void *info)
{
	struct chx_value *value;
	if (!unpack_args(c, "set-max-stack-depth", args, true, 1, &value))
		return NULL;

	if (cheax_type_of(value) != CHEAX_INT) {
		cry(c, "set-max-stack-depth", CHEAX_ETYPE, "expected integer argument");
		return NULL;
	}

	struct chx_int *int_arg = (struct chx_int *)value;
	int ivalue = int_arg->value;

	if (ivalue > 0)
		cheax_set_max_stack_depth(c, ivalue);
	else
		cry(c, "set-max-stack-depth", CHEAX_EVALUE, "maximum stack depth must be positive");

	return NULL;
}

static struct chx_value *
create_func(CHEAX *c,
            const char *name,
            struct chx_list *args,
            int type)
{
	if (args == NULL) {
		cry(c, name, CHEAX_EMATCH, "expected arguments");
		return NULL;
	}

	struct chx_value *arg_list = args->value;
	struct chx_list *body = args->next;

	if (body == NULL) {
		cry(c, name, CHEAX_EMATCH, "expected body");
		return NULL;
	}

	struct chx_func *res = cheax_alloc(c, sizeof(struct chx_func), type);
	res->args = arg_list;
	res->body = body;
	res->lexenv = c->env;
	return &res->base;
}

static struct chx_value *
builtin_fn(CHEAX *c, struct chx_list *args, void *info)
{
	return create_func(c, "fn", args, CHEAX_FUNC);
}
static struct chx_value *
builtin_macro(CHEAX *c, struct chx_list *args, void *info)
{
	return create_func(c, "macro", args, CHEAX_MACRO);
}

static struct chx_value *
builtin_eval(CHEAX *c, struct chx_list *args, void *info)
{
	struct chx_value *arg;
	if (!unpack_args(c, "eval", args, true, 1, &arg))
		return NULL;

	return cheax_eval(c, arg);
}

static struct chx_value *
builtin_case(CHEAX *c, struct chx_list *args, void *info)
{
	if (args == NULL) {
		cry(c, "case", CHEAX_EMATCH, "invalid case");
		return NULL;
	}

	struct chx_value *what = cheax_eval(c, args->value);
	cheax_ft(c, pad);

	/* pattern-value-pair */
	for (struct chx_list *pvp = args->next; pvp; pvp = pvp->next) {
		struct chx_value *pair = pvp->value;
		if (cheax_type_of(pair) != CHEAX_LIST) {
			cry(c, "case", CHEAX_EMATCH, "pattern-value pair expected");
			return NULL;
		}

		struct chx_list *cons_pair = (struct chx_list *)pair;
		struct chx_value *pan = cons_pair->value;

		cheax_push_env(c);

		if (!cheax_match(c, pan, what, CHEAX_READONLY)) {
			cheax_pop_env(c);
			continue;
		}

		/* pattern matches! */
		cheax_ref(c, what);

		struct chx_value *retval = NULL;
		for (struct chx_list *val = cons_pair->next; val != NULL; val = val->next) {
			retval = cheax_eval(c, val->value);
			cheax_ft(c, pad2);
		}

pad2:
		cheax_unref(c, what);
		cheax_pop_env(c);
		return retval;
	}

	cry(c, "case", CHEAX_EMATCH, "non-exhaustive pattern");

pad:
	return NULL;
}

static struct chx_value *
builtin_let(CHEAX *c, struct chx_list *args, void *info)
{
	struct chx_value *res = NULL;

	if (args == NULL) {
		cry(c, "let", CHEAX_EMATCH, "invalid let");
		return NULL;
	}

	struct chx_value *pairsv = args->value;
	if (cheax_type_of(pairsv) != CHEAX_LIST) {
		cry(c, "let", CHEAX_ETYPE, "invalid let");
		return NULL;
	}

	cheax_push_env(c);

	for (struct chx_list *pairs = (struct chx_list *)pairsv;
	     pairs != NULL;
	     pairs = pairs->next)
	{
		struct chx_value *pairv = pairs->value;
		if (cheax_type_of(pairv) != CHEAX_LIST) {
			cry(c, "let", CHEAX_ETYPE, "expected list of lists in first arg");
			goto pad;
		}

		struct chx_list *pair = (struct chx_list *)pairv;
		if (pair->next == NULL || pair->next->next != NULL) {
			cry(c, "let", CHEAX_EVALUE, "expected list of match pairs in first arg");
			goto pad;
		}

		struct chx_value *pan = pair->value, *match = pair->next->value;
		match = cheax_eval(c, match);
		cheax_ft(c, pad);

		if (!cheax_match(c, pan, match, CHEAX_READONLY)) {
			cry(c, "let", CHEAX_EMATCH, "failed match in match pair list");
			goto pad;
		}
	}

	if (args->next == NULL) {
		cry(c, "let", CHEAX_EMATCH, "expected body");
		goto pad;
	}

	struct chx_value *retval;
	for (struct chx_list *cons = args->next;
	     cons != NULL;
	     cons = cons->next)
	{
		retval = cheax_eval(c, cons->value);
		cheax_ft(c, pad);
	}

	res = retval;
pad:
	cheax_pop_env(c);
	return res;
}


static bool
is_numeric_type(struct chx_value *val)
{
	int ty = cheax_type_of(val);
	return (ty == CHEAX_INT) || (ty == CHEAX_DOUBLE);
}

static struct chx_value *
do_aop(CHEAX *c,
       const char *name,
       struct chx_list *args,
       int    (*iop)(CHEAX *, int   , int   ),
       double (*fop)(CHEAX *, double, double))
{
	struct chx_value *l, *r;
	if (!unpack_args(c, name, args, true, 2, &l, &r))
		return NULL;

	if (!is_numeric_type(l) || !is_numeric_type(r)) {
		cry(c, name, CHEAX_ETYPE, "invalid types for operation");
		return NULL;
	}

	if (cheax_type_of(l) == CHEAX_INT && cheax_type_of(r) == CHEAX_INT) {
		int li = ((struct chx_int *)l)->value;
		int ri = ((struct chx_int *)r)->value;
		int res = iop(c, li, ri);
		if (cheax_errstate(c) == CHEAX_THROWN)
			return NULL;

		return &cheax_int(c, res)->base;
	}

	if (fop == NULL) {
		cry(c, name, CHEAX_ETYPE, "invalid operation on floating point numbers");
		return NULL;
	}

	double ld, rd;
	try_vtod(l, &ld);
	try_vtod(r, &rd);
	return &cheax_double(c, fop(c, ld, rd))->base;
}

static int
iop_add(CHEAX *c, int    a, int    b)
{
	if ((b > 0) && (a > INT_MAX - b)) {
		cry(c, "+", CHEAX_EOVERFLOW, "integer overflow");
		return 0;
	}
	if ((b < 0) && (a < INT_MIN - b)) {
		cry(c, "+", CHEAX_EOVERFLOW, "integer underflow");
		return 0;
	}

	return a + b;
}
static double
fop_add(CHEAX *c, double a, double b)
{
	return a + b;
}
static int
iop_sub(CHEAX *c, int    a, int    b)
{
	if ((b > 0) && (a < INT_MIN + b)) {
		cry(c, "+", CHEAX_EOVERFLOW, "integer underflow");
		return 0;
	}
	if ((b < 0) && (a > INT_MAX + b)) {
		cry(c, "+", CHEAX_EOVERFLOW, "integer overflow");
		return 0;
	}

	return a - b;
}
static double
fop_sub(CHEAX *c, double a, double b)
{
	return a - b;
}
static int
iop_mul(CHEAX *c, int    a, int    b)
{
	if (((a == -1) && (b == INT_MIN))
	 || ((b == -1) && (a == INT_MIN)))
	{
		cry(c, "*", CHEAX_EOVERFLOW, "integer overflow");
		return 0;
	}

	if (b != 0) {
		if (a > INT_MAX / b) {
			cry(c, "*", CHEAX_EOVERFLOW, "integer overflow");
			return 0;
		}
		if (a < INT_MIN / b) {
			cry(c, "+", CHEAX_EOVERFLOW, "integer underflow");
			return 0;
		}
	}

	return a * b;
}
static double
fop_mul(CHEAX *c, double a, double b)
{
	return a * b;
}
static int
iop_div(CHEAX *c, int    a, int    b)
{
	if (b == 0) {
		cry(c, "/", CHEAX_EDIVZERO, "division by zero");
		return 0;
	}

	return a / b;
}
static double
fop_div(CHEAX *c, double a, double b)
{
	return a / b;
}
static int
iop_mod(CHEAX *c, int    a, int    b)
{
	if (b == 0) {
		cry(c, "%", CHEAX_EDIVZERO, "division by zero");
		return 0;
	}

	return a % b;
}

static struct chx_value *
builtin_add(CHEAX *c, struct chx_list *args, void *info)
{
	return do_aop(c, "+", args, iop_add, fop_add);
}
static struct chx_value *
builtin_sub(CHEAX *c, struct chx_list *args, void *info)
{
	return do_aop(c, "-", args, iop_sub, fop_sub);
}
static struct chx_value *
builtin_mul(CHEAX *c, struct chx_list *args, void *info)
{
	return do_aop(c, "*", args, iop_mul, fop_mul);
}
static struct chx_value *
builtin_div(CHEAX *c, struct chx_list *args, void *info)
{
	return do_aop(c, "/", args, iop_div, fop_div);
}
static struct chx_value *
builtin_mod(CHEAX *c, struct chx_list *args, void *info)
{
	return do_aop(c, "%", args, iop_mod, NULL);
}

static struct chx_value *
builtin_eq(CHEAX *c, struct chx_list *args, void *info)
{
	struct chx_value *l, *r;
	if (!unpack_args(c, "=", args, true, 2, &l, &r))
		return NULL;

	return &cheax_bool(c, cheax_eq(c, l, r))->base;
}
static struct chx_value *
builtin_ne(CHEAX *c, struct chx_list *args, void *info)
{
	struct chx_value *l, *r;
	if (!unpack_args(c, "!=", args, true, 2, &l, &r))
		return NULL;

	return &cheax_bool(c, !cheax_eq(c, l, r))->base;
}

static struct chx_value *
do_cmp(CHEAX *c,
       const char *name,
       struct chx_list *args,
       bool lt, bool eq, bool gt)
{
	struct chx_value *l, *r;
	if (!unpack_args(c, name, args, true, 2, &l, &r))
		return NULL;

	if (!is_numeric_type(l) || !is_numeric_type(r)) {
		cry(c, name, CHEAX_ETYPE, "invalid types for operation");
		return NULL;
	}

	bool is_lt, is_eq, is_gt;

	if (cheax_type_of(l) == CHEAX_INT && cheax_type_of(r) == CHEAX_INT) {
		int li = ((struct chx_int *)l)->value;
		int ri = ((struct chx_int *)r)->value;
		is_lt = li < ri;
		is_eq = li == ri;
		is_gt = li > ri;
	} else {
		double ld, rd;
		try_vtod(l, &ld);
		try_vtod(r, &rd);
		is_lt = ld < rd;
		is_eq = ld == rd;
		is_gt = ld > rd;
	}

	return &cheax_bool(c, ((lt && is_lt) || (eq && is_eq) || (gt && is_gt)))->base;
}

static struct chx_value *
builtin_lt(CHEAX *c, struct chx_list *args, void *info)
{
	return do_cmp(c, "<", args, 1, 0, 0);
}
static struct chx_value *
builtin_le(CHEAX *c, struct chx_list *args, void *info)
{
	return do_cmp(c, "<=", args, 1, 1, 0);
}
static struct chx_value *
builtin_gt(CHEAX *c, struct chx_list *args, void *info)
{
	return do_cmp(c, ">", args, 0, 0, 1);
}
static struct chx_value *
builtin_ge(CHEAX *c, struct chx_list *args, void *info)
{
	return do_cmp(c, ">=", args, 0, 1, 1);
}

void
export_builtins(CHEAX *c)
{
	c->fhandle_type = cheax_new_type(c, "FileHandle", CHEAX_USER_PTR);

	struct {
		const char *name;
		chx_func_ptr fn;
	} btns[] = {
		{ "cheax-version", builtin_cheax_version },
		{ "read-from", builtin_read_from },
		{ "print-to", builtin_print_to },
		{ "put-to", builtin_put_to },
		{ "format", builtin_format },
		{ "bytes", builtin_bytes },
		{ "error-code", builtin_error_code },
		{ "error-msg", builtin_error_msg },
		{ "throw", builtin_throw },
		{ "try", builtin_try },
		{ "new-error-code", builtin_new_error_code },
		{ "defsym", builtin_defsym },
		{ "var", builtin_var },
		{ "def", builtin_def },
		{ "set", builtin_set },
		{ "env", builtin_env },
		{ ":", builtin_prepend },
		{ "type-of", builtin_type_of },
		{ "get-max-stack-depth", builtin_get_max_stack_depth },
		{ "fn", builtin_fn },
		{ "macro", builtin_macro },
		{ "eval", builtin_eval },
		{ "case", builtin_case },
		{ "let", builtin_let },
		{ "+", builtin_add },
		{ "-", builtin_sub },
		{ "*", builtin_mul },
		{ "/", builtin_div },
		{ "%", builtin_mod },
		{ "=", builtin_eq },
		{ "!=", builtin_ne },
		{ "<", builtin_lt },
		{ "<=", builtin_le },
		{ ">", builtin_gt },
		{ ">=", builtin_ge },
	};

	size_t nbtns = sizeof(btns) / sizeof(btns[0]);

	for (int i = 0; i < nbtns; ++i)
		cheax_defmacro(c, btns[i].name, btns[i].fn, NULL);

	cheax_var(c, "stdin", &cheax_user_ptr(c, stdin, c->fhandle_type)->base, CHEAX_READONLY);
	cheax_var(c, "stdout", &cheax_user_ptr(c, stdout, c->fhandle_type)->base, CHEAX_READONLY);
	cheax_var(c, "stderr", &cheax_user_ptr(c, stderr, c->fhandle_type)->base, CHEAX_READONLY);
}

void
cheax_load_extra_builtins(CHEAX *c, int builtins)
{
	if (builtins & CHEAX_FILE_IO) {
		cheax_defmacro(c, "fopen", builtin_fopen, NULL);
		cheax_defmacro(c, "fclose", builtin_fclose, NULL);
	}

	if (builtins & CHEAX_SET_MAX_STACK_DEPTH)
		cheax_defmacro(c, "set-max-stack-depth", builtin_set_max_stack_depth, NULL);

	if (builtins & CHEAX_GC_BUILTIN) {
		cheax_defmacro(c, "gc", builtin_gc, NULL);
		cheax_defmacro(c, "get-used-memory", builtin_get_used_memory, NULL);
	}

	if (builtins & CHEAX_EXIT_BUILTIN) {
		cheax_defmacro(c, "exit", builtin_exit, NULL);
	}
}
