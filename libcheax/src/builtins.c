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

#define DECL_BUILTIN(cname) \
static struct chx_value *builtin_##cname(CHEAX *c, struct chx_list *args)

static struct chx_int yes = { { CHEAX_INT }, 1 }, no = { { CHEAX_INT }, 0 };

DECL_BUILTIN(cheax_version);
DECL_BUILTIN(fopen);
DECL_BUILTIN(fclose);
DECL_BUILTIN(read_from);
DECL_BUILTIN(print_to);
DECL_BUILTIN(format);
DECL_BUILTIN(bytes);
DECL_BUILTIN(error_code);
DECL_BUILTIN(error_msg);
DECL_BUILTIN(throw);
DECL_BUILTIN(try);
DECL_BUILTIN(new_error_code);
DECL_BUILTIN(exit);
DECL_BUILTIN(var);
DECL_BUILTIN(const);
DECL_BUILTIN(set);
DECL_BUILTIN(prepend);
DECL_BUILTIN(gc);
DECL_BUILTIN(get_used_memory);
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
DECL_BUILTIN(ne);
DECL_BUILTIN(lt);
DECL_BUILTIN(le);
DECL_BUILTIN(gt);
DECL_BUILTIN(ge);

void
export_builtins(CHEAX *c)
{
	c->fhandle_type = cheax_new_type(c, "FileHandle", CHEAX_USER_PTR);

	cheax_defmacro(c, "cheax-version", builtin_cheax_version);
	cheax_defmacro(c, "read-from", builtin_read_from);
	cheax_defmacro(c, "print-to", builtin_print_to);
	cheax_defmacro(c, "format", builtin_format);
	cheax_defmacro(c, "bytes", builtin_bytes);
	cheax_defmacro(c, "error-code", builtin_error_code);
	cheax_defmacro(c, "error-msg", builtin_error_msg);
	cheax_defmacro(c, "throw", builtin_throw);
	cheax_defmacro(c, "try", builtin_try);
	cheax_defmacro(c, "new-error-code", builtin_new_error_code);
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
	cheax_defmacro(c, "!=", builtin_ne);
	cheax_defmacro(c, "<", builtin_lt);
	cheax_defmacro(c, "<=", builtin_le);
	cheax_defmacro(c, ">", builtin_gt);
	cheax_defmacro(c, ">=", builtin_ge);

	cheax_var(c, "stdin", &cheax_user_ptr(c, stdin, c->fhandle_type)->base, CHEAX_READONLY);
	cheax_var(c, "stdout", &cheax_user_ptr(c, stdout, c->fhandle_type)->base, CHEAX_READONLY);
	cheax_var(c, "stderr", &cheax_user_ptr(c, stderr, c->fhandle_type)->base, CHEAX_READONLY);
}

void
cheax_load_extra_builtins(CHEAX *c, enum chx_builtins builtins)
{
	if (builtins & CHEAX_FILE_IO) {
		cheax_defmacro(c, "fopen", builtin_fopen);
		cheax_defmacro(c, "fclose", builtin_fclose);
	}

	if (builtins & CHEAX_SET_MAX_STACK_DEPTH)
		cheax_defmacro(c, "set-max-stack-depth", builtin_set_max_stack_depth);

	if (builtins & CHEAX_GC_BUILTIN) {
		cheax_defmacro(c, "gc", builtin_gc);
		cheax_defmacro(c, "get-used-memory", builtin_get_used_memory);
	}

	if (builtins & CHEAX_EXIT_BUILTIN) {
		cheax_defmacro(c, "exit", builtin_exit);
	}
}

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
			cry(c, fname, CHEAX_EMATCH, "Expected %d arguments (got %d)", num, i);
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
		cry(c, fname, CHEAX_EMATCH, "Expected only %d arguments", num);
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
builtin_cheax_version(CHEAX *c, struct chx_list *args)
{
	if (!unpack_args(c, "cheax-version", args, false, 0))
		return NULL;

	return &cheax_string(c, cheax_version())->base;
}

static struct chx_value *
builtin_fopen(CHEAX *c, struct chx_list *args)
{
	struct chx_value *fname, *mode;
	if (!unpack_args(c, "fopen", args, true, 2, &fname, &mode))
		return NULL;

	if (cheax_get_type(fname) != CHEAX_STRING || cheax_get_type(mode) != CHEAX_STRING) {
		cry(c, "fopen", CHEAX_ETYPE, "Invalid argument type");
		return NULL;
	}

	const char *fname_str = ((struct chx_string *)fname)->value;
	const char *mode_str = ((struct chx_string *)mode)->value;
	FILE *f = fopen(fname_str, mode_str);

	return &cheax_user_ptr(c, f, c->fhandle_type)->base;
}
static struct chx_value *
builtin_fclose(CHEAX *c, struct chx_list *args)
{
	struct chx_value *handle;
	if (!unpack_args(c, "fclose", args, true, 1, &handle))
		return NULL;

	if (cheax_get_type(handle) != c->fhandle_type) {
		cry(c, "fclose", CHEAX_ETYPE, "Invalid argument type");
		return NULL;
	}

	FILE *f = (FILE *)((struct chx_user_ptr *)handle)->value;
	fclose(f);

	return NULL;
}
static struct chx_value *
builtin_read_from(CHEAX *c, struct chx_list *args)
{
	struct chx_value *handle;
	if (!unpack_args(c, "read-from", args, true, 1, &handle))
		return NULL;

	if (cheax_get_type(handle) != c->fhandle_type) {
		cry(c, "fclose", CHEAX_ETYPE, "Invalid argument type");
		return NULL;
	}

	FILE *f = (FILE *)((struct chx_user_ptr *)handle)->value;
	return cheax_read(c, f);
}
static struct chx_value *
builtin_print_to(CHEAX *c, struct chx_list *args)
{
	struct chx_value *handle, *value;
	if (!unpack_args(c, "print-to", args, true, 2, &handle, &value))
		return NULL;

	if (cheax_get_type(handle) != c->fhandle_type) {
		cry(c, "print-to", CHEAX_ETYPE, "Invalid argument type");
		return NULL;
	}

	FILE *f = (FILE *)((struct chx_user_ptr *)handle)->value;
	cheax_print(c, f, value);
	fputc('\n', f);
	return NULL;
}

static struct chx_value *
builtin_format(CHEAX *c, struct chx_list *args)
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
		cry(c, "format", CHEAX_EMATCH, "Expected at least 1 argument (got 0)");
		return NULL;
	}

	struct chx_value *fmt_str_val = ev_args->value;

	if (cheax_get_type(fmt_str_val) != CHEAX_STRING) {
		cry(c, "format", CHEAX_ETYPE, "Expected format string");
		return NULL;
	}

	struct chx_string *fmt_str = (struct chx_string *)fmt_str_val;
	return cheax_format(c, fmt_str->value, ev_args->next);

pad:
	return NULL;
}

static struct chx_value *
builtin_bytes(CHEAX *c, struct chx_list *args)
{
	struct chx_value *arg;
	if (!unpack_args(c, "bytes", args, true, 1, &arg))
		return NULL;

	if (cheax_get_type(arg) != CHEAX_STRING) {
		cry(c, "bytes", CHEAX_ETYPE, "Expected string");
		return NULL;
	}

	struct chx_string *str = (struct chx_string *)arg;
	struct chx_list *bytes = NULL;

	for (int i = (int)str->len - 1; i >= 0; --i)
		bytes = cheax_list(c, &cheax_int(c, str->value[i])->base, bytes);

	return &bytes->base;
}

static struct chx_value *
builtin_const(CHEAX *c, struct chx_list *args)
{
	struct chx_value *idval, *setto;
	if (!unpack_args(c, "const", args, false, 2, &idval, &setto))
		return NULL;

	setto = cheax_eval(c, setto);
	cheax_ft(c, pad);

	struct variable *prev_top = c->locals_top;

	if (!cheax_match(c, idval, setto)) {
		cry(c, "const", CHEAX_EMATCH, "Invalid pattern");
		return NULL;
	}

	if (cheax_get_type(setto) == CHEAX_FUNC) {
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

static struct chx_value *
builtin_error_code(CHEAX *c, struct chx_list *args)
{
	if (!unpack_args(c, "error-code", args, false, 0))
		return NULL;

	struct chx_value *res = &cheax_int(c, c->error.code)->base;
	res->type = CHEAX_ERRORCODE;
	return res;
}
static struct chx_value *
builtin_error_msg(CHEAX *c, struct chx_list *args)
{
	if (!unpack_args(c, "error-msg", args, false, 0))
		return NULL;

	return &c->error.msg->base;
}
static struct chx_value *
builtin_throw(CHEAX *c, struct chx_list *args)
{
	struct chx_value *code, *msg;
	if (!unpack_args(c, "throw", args, true, 2, &code, &msg))
		return NULL;

	if (cheax_get_type(code) != CHEAX_ERRORCODE) {
		cry(c, "throw", CHEAX_ETYPE, "Expected error code");
		return NULL;
	}

	if (((struct chx_int *)code)->value == 0) {
		cry(c, "throw", CHEAX_EVALUE, "Cannot throw error code 0");
		return NULL;
	}

	if (msg != NULL && cheax_get_type(msg) != CHEAX_STRING) {
		cry(c, "throw", CHEAX_ETYPE, "Expected string message");
		return NULL;
	}

	cheax_throw(c, ((struct chx_int *)code)->value, (struct chx_string *)msg);
	return NULL;
}
static struct chx_value *
builtin_try(CHEAX *c, struct chx_list *args)
{
	if (args == NULL) {
		cry(c, "try", CHEAX_EMATCH, "Expected at least two arguments");
		return NULL;
	}

	struct chx_value *block = args->value;
	struct chx_list *catch_blocks = args->next;
	if (catch_blocks == NULL) {
		cry(c, "try", CHEAX_EMATCH, "Expected at least one catch/finally block");
		return NULL;
	}

	/* The item such that for the final catch block cb, we have
	 * cb->next == finally_block */
	struct chx_list *finally_block = NULL;

	for (struct chx_list *cb = catch_blocks; cb != NULL; cb = cb->next) {
		struct chx_value *cb_value = cb->value;
		if (cheax_get_type(cb_value) != CHEAX_LIST) {
			cry(c, "try", CHEAX_ETYPE, "Catch/finally blocks must be s-expressions");
			return NULL;
		}

		struct chx_list *cb_list = (struct chx_list *)cb_value;
		bool is_id = cheax_get_type(cb_list->value) == CHEAX_ID;

		struct chx_id *keyword = (struct chx_id *)cb_list->value;
		if (is_id && 0 == strcmp("catch", keyword->id)) {
			if (cb_list->next == NULL || cb_list->next->next == NULL) {
				cry(c, "catch", CHEAX_EMATCH, "Expected at least two arguments");
				return NULL;
			}
		} else if (is_id && 0 == strcmp("finally", keyword->id)) {
			if (cb->next != NULL) {
				cry(c, "finally", CHEAX_EVALUE, "Unexpected values after finally block");
				return NULL;
			}

			finally_block = cb;
		} else {
			cry(c, "try", CHEAX_EMATCH, "Expected `catch' or `finally' keyword");
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

		if (cheax_get_type(errcodes) != CHEAX_LIST)
			errcodes = &cheax_list(c, errcodes, NULL)->base;

		for (struct chx_list *enode = (struct chx_list *)errcodes; enode != NULL; enode = enode->next) {
			struct chx_value *code = enode->value;
			if (cheax_get_type(code) != CHEAX_ERRORCODE) {
				cry(c, "catch", CHEAX_ETYPE, "Expected error code or list thereof");
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
builtin_new_error_code(CHEAX *c, struct chx_list *args)
{
	struct chx_value *errname_id;
	if (!unpack_args(c, "new-error-code", args, false, 1, &errname_id))
		return NULL;

	if (cheax_get_type(errname_id) != CHEAX_ID) {
		cry(c, "new-error-code", CHEAX_ETYPE, "Expected ID");
		return NULL;
	}

	const char *errname = ((struct chx_id *)errname_id)->id;
	cheax_new_error_code(c, errname);

	return NULL;
}

static struct chx_value *
builtin_exit(CHEAX *c, struct chx_list *args)
{
	int code = 0;

	if (args != NULL) {
		struct chx_value *code_val;
		if (!unpack_args(c, "exit", args, false, 1, &code_val))
			return NULL;

		if (cheax_get_type(code_val) != CHEAX_INT) {
			cry(c, "exit", CHEAX_ETYPE, "Expected integer exit code");
			return NULL;
		}

		code = ((struct chx_int *)code_val)->value;
	}

	exit(code);
}

static struct chx_value *
builtin_var(CHEAX *c, struct chx_list *args)
{
	struct chx_value *idval, *setto = NULL;
	if (!((args && args->next && unpack_args(c, "var", args, false, 2, &idval, &setto))
	 || unpack_args(c, "var", args, false, 1, &idval)))
	{
		return NULL;
	}

	setto = cheax_eval(c, setto);
	cheax_ft(c, pad);

	if (!cheax_match(c, idval, setto)) {
		cry(c, "var", CHEAX_EMATCH, "Invalid pattern");
		return NULL;
	}

pad:
	return NULL;
}

static struct chx_value *
builtin_set(CHEAX *c, struct chx_list *args)
{
	struct chx_value *idval, *setto;
	if (!unpack_args(c, "set", args, false, 2, &idval, &setto))
		return NULL;

	if (cheax_get_type(idval) != CHEAX_ID) {
		cry(c, "set", CHEAX_ETYPE, "Expected identifier");
		return NULL;
	}

	setto = cheax_eval(c, setto);
	cheax_ft(c, pad);
	cheax_set(c, ((struct chx_id *)idval)->id, setto);

pad:
	return NULL;

}

static struct chx_value *
builtin_prepend(CHEAX *c, struct chx_list *args)
{
	struct chx_value *head, *tail;
	if (!unpack_args(c, ":", args, true, 2, &head, &tail))
		return NULL;

	int tailty = cheax_get_type(tail);
	if (tailty != CHEAX_NIL && tailty != CHEAX_LIST) {
		cry(c, ":", CHEAX_ETYPE, "Improper list not allowed");
		return NULL;
	}

	return &cheax_list(c, head, (struct chx_list *)tail)->base;
}

static struct chx_value *
builtin_gc(CHEAX *c, struct chx_list *args)
{
	if (unpack_args(c, "gc", args, false, 0))
		cheax_force_gc(c);

	return NULL;
}
static struct chx_value *
builtin_get_used_memory(CHEAX *c, struct chx_list *args)
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
builtin_get_type(CHEAX *c, struct chx_list *args)
{
	struct chx_value *val;
	if (!unpack_args(c, "get-type", args, true, 1, &val))
		return NULL;

	struct chx_int *res = cheax_int(c, cheax_get_type(val));
	res->base.type = CHEAX_TYPECODE;
	return &res->base;
}

static struct chx_value *
builtin_get_max_stack_depth(CHEAX *c, struct chx_list *args)
{
	if (!unpack_args(c, "get-max-stack-depth", args, false, 0))
		return NULL;

	return &cheax_int(c, c->max_stack_depth)->base;
}

static struct chx_value *
builtin_set_max_stack_depth(CHEAX *c, struct chx_list *args)
{
	struct chx_value *value;
	if (!unpack_args(c, "set-max-stack-depth", args, true, 1, &value))
		return NULL;

	if (cheax_get_type(value) != CHEAX_INT) {
		cry(c, "set-max-stack-depth", CHEAX_ETYPE, "Expected integer argument");
		return NULL;
	}

	struct chx_int *int_arg = (struct chx_int *)value;
	int ivalue = int_arg->value;

	if (ivalue > 0)
		cheax_set_max_stack_depth(c, ivalue);
	else
		cry(c, "set-max-stack-depth", CHEAX_EVALUE, "Maximum stack depth must be positive");

	return NULL;
}

static struct chx_value *
create_func(CHEAX *c,
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

	struct chx_func *res = cheax_alloc(c, sizeof(struct chx_func));
	res->base.type = CHEAX_FUNC;
	res->eval_args = eval_args;
	res->args = arg_list;
	res->body = body;
	res->locals_top = c->locals_top;
	return &res->base;
}

static struct chx_value *
builtin_lambda(CHEAX *c, struct chx_list *args)
{
	return create_func(c, "\\", args, true);
}
static struct chx_value *
builtin_macro_lambda(CHEAX *c, struct chx_list *args)
{
	return create_func(c, "\\\\", args, false);
}

static struct chx_value *
builtin_eval(CHEAX *c, struct chx_list *args)
{
	struct chx_value *arg;
	if (!unpack_args(c, "eval", args, true, 1, &arg))
		return NULL;

	return cheax_eval(c, arg);
}

static struct chx_value *
builtin_case(CHEAX *c, struct chx_list *args)
{
	if (args == NULL) {
		cry(c, "case", CHEAX_EMATCH, "Invalid case");
		return NULL;
	}

	struct chx_value *what = cheax_eval(c, args->value);
	cheax_ft(c, pad);

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
		if (!cheax_match(c, pan, what)) {
			c->locals_top = top_before;
			continue;
		}

		/* pattern matches! */
		cheax_ref(c, what);

		struct chx_value *retval = NULL;
		for (struct chx_list *val = cons_pair->next; val; val = val->next) {
			retval = cheax_eval(c, val->value);
			cheax_ft(c, pad2);
		}
		c->locals_top = top_before;

pad2:
		cheax_unref(c, what);
		return retval;
	}

	cry(c, "case", CHEAX_EMATCH, "Non-exhaustive pattern");

pad:
	return NULL;
}


static bool
is_numeric_type(struct chx_value *val)
{
	int ty = cheax_get_type(val);
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
		cry(c, name, CHEAX_ETYPE, "Invalid types for operation");
		return NULL;
	}

	if (cheax_get_type(l) == CHEAX_INT && cheax_get_type(r) == CHEAX_INT) {
		int li = ((struct chx_int *)l)->value;
		int ri = ((struct chx_int *)r)->value;
		int res = iop(c, li, ri);
		if (cheax_errstate(c) == CHEAX_THROWN)
			return NULL;

		return &cheax_int(c, res)->base;
	}

	if (fop == NULL) {
		cry(c, name, CHEAX_ETYPE, "Invalid operation on floating point numbers");
		return NULL;
	}

	double ld, rd;
	try_convert_to_double(l, &ld);
	try_convert_to_double(r, &rd);
	return &cheax_double(c, fop(c, ld, rd))->base;
}

static int
iop_add(CHEAX *c, int    a, int    b)
{
	if ((b > 0) && (a > INT_MAX - b)) {
		cry(c, "+", CHEAX_EOVERFLOW, "Integer overflow");
		return 0;
	}
	if ((b < 0) && (a < INT_MIN - b)) {
		cry(c, "+", CHEAX_EOVERFLOW, "Integer underflow");
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
		cry(c, "+", CHEAX_EOVERFLOW, "Integer underflow");
		return 0;
	}
	if ((b < 0) && (a > INT_MAX + b)) {
		cry(c, "+", CHEAX_EOVERFLOW, "Integer overflow");
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
		cry(c, "*", CHEAX_EOVERFLOW, "Integer overflow");
		return 0;
	}

	if (b != 0) {
		if (a > INT_MAX / b) {
			cry(c, "*", CHEAX_EOVERFLOW, "Integer overflow");
			return 0;
		}
		if (a < INT_MIN / b) {
			cry(c, "+", CHEAX_EOVERFLOW, "Integer underflow");
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
		cry(c, "/", CHEAX_EDIVZERO, "Division by zero");
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
		cry(c, "%", CHEAX_EDIVZERO, "Division by zero");
		return 0;
	}

	return a % b;
}

static struct chx_value *
builtin_add(CHEAX *c, struct chx_list *args)
{
	return do_aop(c, "+", args, iop_add, fop_add);
}
static struct chx_value *
builtin_sub(CHEAX *c, struct chx_list *args)
{
	return do_aop(c, "-", args, iop_sub, fop_sub);
}
static struct chx_value *
builtin_mul(CHEAX *c, struct chx_list *args)
{
	return do_aop(c, "*", args, iop_mul, fop_mul);
}
static struct chx_value *
builtin_div(CHEAX *c, struct chx_list *args)
{
	return do_aop(c, "/", args, iop_div, fop_div);
}
static struct chx_value *
builtin_mod(CHEAX *c, struct chx_list *args)
{
	return do_aop(c, "%", args, iop_mod, NULL);
}

static struct chx_value *
builtin_eq(CHEAX *c, struct chx_list *args)
{
	struct chx_value *l, *r;
	if (!unpack_args(c, "=", args, true, 2, &l, &r))
		return NULL;

	return cheax_equals(c, l, r) ? &yes.base : &no.base;
}
static struct chx_value *
builtin_ne(CHEAX *c, struct chx_list *args)
{
	struct chx_value *l, *r;
	if (!unpack_args(c, "!=", args, true, 2, &l, &r))
		return NULL;

	return cheax_equals(c, l, r) ? &no.base : &yes.base;
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
		cry(c, name, CHEAX_ETYPE, "Invalid types for operation");
		return NULL;
	}

	bool is_lt, is_eq, is_gt;

	if (cheax_get_type(l) == CHEAX_INT && cheax_get_type(r) == CHEAX_INT) {
		int li = ((struct chx_int *)l)->value;
		int ri = ((struct chx_int *)r)->value;
		is_lt = li < ri;
		is_eq = li == ri;
		is_gt = li > ri;
	} else {
		double ld, rd;
		try_convert_to_double(l, &ld);
		try_convert_to_double(r, &rd);
		is_lt = ld < rd;
		is_eq = ld == rd;
		is_gt = ld > rd;
	}

	return ((lt && is_lt) || (eq && is_eq) || (gt && is_gt))
	     ? &yes.base : &no.base;
}

static struct chx_value *
builtin_lt(CHEAX *c, struct chx_list *args)
{
	return do_cmp(c, "<", args, 1, 0, 0);
}
static struct chx_value *
builtin_le(CHEAX *c, struct chx_list *args)
{
	return do_cmp(c, "<=", args, 1, 1, 0);
}
static struct chx_value *
builtin_gt(CHEAX *c, struct chx_list *args)
{
	return do_cmp(c, ">", args, 0, 0, 1);
}
static struct chx_value *
builtin_ge(CHEAX *c, struct chx_list *args)
{
	return do_cmp(c, ">=", args, 0, 1, 1);
}
