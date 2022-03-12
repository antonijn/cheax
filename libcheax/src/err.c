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
#include <stdarg.h>
#include <stdlib.h>

#include "core.h"
#include "err.h"
#include "print.h"
#include "unpack.h"

/* declare associative array of builtin error codes and their names */
CHEAX_BUILTIN_ERROR_NAMES(bltn_error_names);

static const char *
errname(CHEAX *c, int code)
{
	if (code >= CHEAX_EUSER0) {
		size_t idx = code - CHEAX_EUSER0;
		if (idx >= c->user_error_names.len) {
			cry(c, "errname", CHEAX_EAPI, "invalid user error code");
			return NULL;
		}
		return c->user_error_names.array[idx];
	}

	/* builtin error code, binary search */

	int lo = 0, hi = sizeof(bltn_error_names) / sizeof(bltn_error_names[0]);
	while (lo <= hi) {
		int pivot = (lo + hi) / 2;
		int pivot_code = bltn_error_names[pivot].code;
		if (pivot_code == code)
			return bltn_error_names[pivot].name;
		if (pivot_code < code)
			lo = pivot + 1;
		else
			hi = pivot - 1;
	}

	cry(c, "errname", CHEAX_EAPI, "invalid error code");
	return NULL;
}
int
cheax_errstate(CHEAX *c)
{
	return c->error.state;
}
int
cheax_errno(CHEAX *c)
{
	return c->error.code;
}
void
cheax_perror(CHEAX *c, const char *s)
{
	int err = cheax_errno(c);
	if (err == 0)
		return;

	bt_print(c);

	if (s != NULL)
		fprintf(stderr, "%s: ", s);

	if (c->error.msg != NULL)
		fprintf(stderr, "%.*s ", (int)c->error.msg->len, c->error.msg->value);

	const char *ename = errname(c, err);
	if (ename != NULL)
		fprintf(stderr, "[%s]", ename);
	else
		fprintf(stderr, "[code %x]", err);

	fprintf(stderr, "\n");
}
void
cheax_clear_errno(CHEAX *c)
{
	c->error.state = CHEAX_RUNNING;
	c->error.code = 0;
	c->error.msg = NULL;
	c->bt.len = 0;
	c->bt.truncated = false;
}
void
cheax_throw(CHEAX *c, int code, struct chx_string *msg)
{
	if (code == 0) {
		cry(c, "throw", CHEAX_EAPI, "cannot throw error code 0");
		return;
	}

	c->error.state = CHEAX_THROWN;
	c->error.code = code;
	c->error.msg = msg;
}
int
cheax_new_error_code(CHEAX *c, const char *name)
{
	if (name == NULL) {
		cry(c, "new_error_code", CHEAX_EAPI, "`name' cannot be NULL");
		return -1;
	}

	int code = CHEAX_EUSER0 + c->user_error_names.len;

	if (c->user_error_names.len + 1 > c->user_error_names.cap) {
		size_t new_len, new_cap;
		new_len = c->user_error_names.len + 1;
		new_cap = new_len + (new_len / 2);

		void *new_array = cheax_realloc(c,
		                                c->user_error_names.array,
		                                new_cap * sizeof(const char *));
		if (new_array == NULL)
			return -1;

		c->user_error_names.array = new_array;
		c->user_error_names.cap = new_cap;
	}

	cheax_def(c, name, &errorcode(c, code)->base, CHEAX_EREADONLY);
	cheax_ft(c, pad);

	c->user_error_names.array[code - CHEAX_EUSER0] = name;
	++c->user_error_names.len;

	return code;
pad:
	return -1;
}

int
bt_init(CHEAX *c, size_t limit)
{
	c->bt.len = c->bt.limit = 0;
	c->bt.last_call = NULL;
	c->bt.truncated = false;

	cheax_free(c, c->bt.array);
	c->bt.array = cheax_calloc(c, limit, sizeof(c->bt.array[0]));
	if (c->bt.array == NULL)
		return -1;

	c->bt.limit = limit;
	return 0;
}

void
bt_add(CHEAX *c)
{
	if (c->bt.last_call == NULL)
		return;

	if (c->bt.len >= c->bt.limit) {
		c->bt.truncated = true;
		return;
	}

	struct chx_list *last_call = c->bt.last_call;
	size_t idx = c->bt.len++;

	struct snostrm ss;
	snostrm_init(&ss, c->bt.array[idx].msg, sizeof(c->bt.array[idx].msg));
	ostrm_show(c, &ss.strm, &last_call->base);
	strcpy(c->bt.array[idx].msg + sizeof(c->bt.array[idx].msg) - 4, "...");

	if (has_flag(last_call->base.rtflags, DEBUG_LIST)) {
		struct debug_list *dbg = (struct debug_list *)last_call;
		c->bt.array[idx].info = dbg->info;
	} else {
		c->bt.array[idx].info.file = "<filename unknown>";
		c->bt.array[idx].info.pos = -1;
		c->bt.array[idx].info.line = -1;
	}
}

void
bt_print(CHEAX *c)
{
	if (c->bt.len == 0)
		return;

	if (c->bt.truncated)
		fprintf(stderr, "Backtrace (limited to last %zd calls):\n", c->bt.limit);
	else
		fprintf(stderr, "Backtrace:\n");

	for (size_t i = c->bt.len; i >= 1; --i) {
		struct bt_entry ent = c->bt.array[i - 1];
		fprintf(stderr, "  File \"%s\"", ent.info.file);
		if (ent.info.line > 0)
			fprintf(stderr, ", line %d", ent.info.line);
		fprintf(stderr, ": %s\n", ent.msg);
	}
}

void
cry(CHEAX *c, const char *name, int err, const char *frmt, ...)
{
	va_list ap;
	size_t preamble_len = strlen(name) + 4;
	struct chx_string *msg = NULL;

	va_start(ap, frmt);
	size_t msglen = preamble_len + vsnprintf(NULL, 0, frmt, ap);
	va_end(ap);

	char *buf = malloc(msglen + 1);
	if (buf != NULL) {
		sprintf(buf, "(%s): ", name);
		va_start(ap, frmt);
		vsnprintf(buf + preamble_len, msglen - preamble_len + 1, frmt, ap);
		va_end(ap);

		/* hack to avoid allocation failure */
		int prev_mem_limit = c->mem_limit;
		c->mem_limit = 0;

		msg = cheax_nstring(c, buf, msglen);

		c->mem_limit = prev_mem_limit;
		free(buf);
	}

	cheax_throw(c, err, msg);
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
bltn_throw(CHEAX *c, struct chx_list *args, void *info)
{
	int code;
	struct chx_string *msg;
	if (unpack(c, "throw", args, "x![s ]?", &code, &msg) < 0)
		return NULL;

	if (code == 0) {
		cry(c, "throw", CHEAX_EVALUE, "cannot throw ENOERR");
		return NULL;
	}

	cheax_throw(c, code, msg);
	return NULL;
}

static int
validate_catch_blocks(CHEAX *c, struct chx_list *catch_blocks, struct chx_list **finally_block)
{
	for (struct chx_list *cb = catch_blocks; cb != NULL; cb = cb->next) {
		struct chx_value *cb_value = cb->value;
		if (cheax_type_of(cb_value) != CHEAX_LIST) {
			cry(c, "try", CHEAX_ETYPE, "catch/finally blocks must be s-expressions");
			return -1;
		}

		struct chx_list *cb_list = (struct chx_list *)cb_value;
		bool is_id = cheax_type_of(cb_list->value) == CHEAX_ID;

		struct chx_id *keyword = (struct chx_id *)cb_list->value;
		if (is_id && 0 == strcmp("catch", keyword->id)) {
			if (cb_list->next == NULL || cb_list->next->next == NULL) {
				cry(c, "catch", CHEAX_EMATCH, "expected at least two arguments");
				return -1;
			}
		} else if (is_id && 0 == strcmp("finally", keyword->id)) {
			if (cb->next != NULL) {
				cry(c, "finally", CHEAX_EVALUE, "unexpected values after finally block");
				return -1;
			}

			*finally_block = cb;
		} else {
			cry(c, "try", CHEAX_EMATCH, "expected `catch' or `finally' keyword");
			return -1;
		}
	}

	return 0;
}

static struct chx_list *
match_catch(CHEAX *c, struct chx_list *catch_blocks, struct chx_list *finally_block)
{
	struct chx_list *cb;
	for (cb = catch_blocks; cb != finally_block; cb = cb->next) {
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
		cheax_ft(c, pad);

		if (cheax_type_of(errcodes) != CHEAX_LIST)
			errcodes = &cheax_list(c, errcodes, NULL)->base;

		struct chx_list *enode;
		for (enode = (struct chx_list *)errcodes; enode != NULL; enode = enode->next) {
			struct chx_value *code = enode->value;
			if (cheax_type_of(code) != CHEAX_ERRORCODE) {
				cry(c, "catch", CHEAX_ETYPE, "expected error code or list thereof");
				return NULL;
			}

			if (((struct chx_int *)code)->value == cheax_errno(c))
				return cb_list;
		}
	}
pad:
	return NULL;
}

static struct chx_value *
run_catch(CHEAX *c, struct chx_list *match)
{
	struct chx_value *retval = NULL;

	/* match, so run catch block code */
	struct chx_list *run_blocks = match->next->next;
	chx_ref run_blocks_ref = cheax_ref(c, run_blocks);

	c->error.state = CHEAX_RUNNING;

	for (struct chx_list *cons = run_blocks; cons != NULL; cons = cons->next) {
		retval = cheax_eval(c, cons->value);
		cheax_ft(c, pad1); /* new error thrown */
	}

	cheax_clear_errno(c); /* error taken care of */
pad1:
	cheax_unref(c, run_blocks, run_blocks_ref);
	return retval;
}

static void
run_finally(CHEAX *c, struct chx_list *finally_block)
{
	struct chx_env *new_env = cheax_push_env(c);
	if (new_env == NULL)
		return;
	new_env->base.rtflags |= NO_ESC_BIT;

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
	cheax_pop_env(c);
}

static struct chx_value *
bltn_try(CHEAX *c, struct chx_list *args, void *info)
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

	if (validate_catch_blocks(c, catch_blocks, &finally_block) < 0)
		return NULL;

	struct chx_env *new_env = cheax_push_env(c);
	if (new_env == NULL)
		return NULL;
	new_env->base.rtflags |= NO_ESC_BIT;

	struct chx_value *retval = cheax_eval(c, block);

	cheax_pop_env(c);

	if (cheax_errstate(c) == CHEAX_THROWN) {
		/* error caught */
		c->error.state = CHEAX_RUNNING;

		/*
		 * We set errno and errmsg here, to allow (catch errno ...),
		 * which matches any error code.
		 */
		if (cheax_push_env(c) == NULL)
			return NULL;
		struct chx_int *code = errorcode(c, cheax_errno(c));
		cheax_def(c, "errno",  &code->base,         CHEAX_READONLY);
		cheax_def(c, "errmsg", &c->error.msg->base, CHEAX_READONLY);

		struct chx_list *match = match_catch(c, catch_blocks, finally_block);
		if (match == NULL)
			c->error.state = CHEAX_THROWN; /* error falls through */
		else
			retval = run_catch(c, match);

		cheax_pop_env(c);
	}

	if (finally_block != NULL) {
		chx_ref retval_ref = cheax_ref(c, retval);
		run_finally(c, finally_block);
		cheax_unref(c, retval, retval_ref);
	}

	return (cheax_errstate(c) == CHEAX_THROWN) ? NULL : retval;
}

static struct chx_value *
bltn_new_error_code(CHEAX *c, struct chx_list *args, void *info)
{
	const char *errname;
	if (0 == unpack(c, "new-error-code", args, "N!", &errname))
		cheax_new_error_code(c, errname);
	return NULL;
}

static void
export_error_names(CHEAX *c)
{
	int num_codes = sizeof(bltn_error_names)
	              / sizeof(bltn_error_names[0]);

	for (int i = 0; i < num_codes; ++i) {
		const char *name = bltn_error_names[i].name;
		int code = bltn_error_names[i].code;
		cheax_def(c, name, &errorcode(c, code)->base, CHEAX_READONLY);
	}
}

void
export_err_bltns(CHEAX *c)
{
	cheax_defmacro(c, "throw",          bltn_throw,          NULL);
	cheax_defmacro(c, "try",            bltn_try,            NULL);
	cheax_defmacro(c, "new-error-code", bltn_new_error_code, NULL);

	export_error_names(c);
}
