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
			cheax_throwf(c, CHEAX_EAPI, "errname(): invalid user error code");
			return NULL;
		}
		return c->user_error_names.array[idx];
	}

	/* builtin error code, binary search */

	int lo = 0, hi = sizeof(bltn_error_names) / sizeof(bltn_error_names[0]);
	while (lo <= hi) {
		int pivot = lo + (hi - lo) / 2;
		int pivot_code = bltn_error_names[pivot].code;
		if (pivot_code == code)
			return bltn_error_names[pivot].name;
		if (pivot_code < code)
			lo = pivot + 1;
		else
			hi = pivot - 1;
	}

	cheax_throwf(c, CHEAX_EAPI, "errname(): invalid error code");
	return NULL;
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
	c->error.code = 0;
	c->error.msg = NULL;
	c->bt.len = 0;
	c->bt.truncated = false;
}
void
cheax_throw(CHEAX *c, int code, struct chx_string *msg)
{
	if (code == 0) {
		cheax_throwf(c, CHEAX_EAPI, "throw(): cannot throw error code 0");
		return;
	}

	c->error.code = code;
	c->error.msg = msg;
	c->bt.len = 0;
	c->bt.truncated = false;
}

void
cheax_throwf(CHEAX *c, int code, const char *fmt, ...)
{
	va_list ap;
	struct chx_string *msg = NULL;
	char sbuf[128];

	va_start(ap, fmt);
	vsnprintf(sbuf, sizeof(sbuf), fmt, ap);
	va_end(ap);

	/* hack to avoid allocation failure */
	int prev_mem_limit = c->mem_limit;
	c->mem_limit = 0;

	msg = cheax_string(c, sbuf);

	c->mem_limit = prev_mem_limit;
	cheax_throw(c, code, msg);
}

int
cheax_new_error_code(CHEAX *c, const char *name)
{
	if (name == NULL) {
		cheax_throwf(c, CHEAX_EAPI, "new_error_code(): `name' cannot be NULL");
		return -1;
	}

	if (cheax_find_error_code(c, name) >= 0) {
		cheax_throwf(c, CHEAX_EAPI, "new_error_code(): error with name %s already exists", name);
		return -1;
	}

	int code = CHEAX_EUSER0 + c->user_error_names.len;

	if (c->user_error_names.len + 1 > c->user_error_names.cap) {
		size_t new_len, new_cap;
		new_len = c->user_error_names.len + 1;
		new_cap = new_len + (new_len / 2);

		void *new_array = cheax_realloc(c,
		                                c->user_error_names.array,
		                                new_cap * sizeof(char *));
		cheax_ft(c, pad);

		c->user_error_names.array = new_array;
		c->user_error_names.cap = new_cap;
	}

	char *store_name = cheax_malloc(c, strlen(name) + 1);
	cheax_ft(c, pad);
	strcpy(store_name, name);

	cheax_def(c, name, &errorcode(c, code)->base, CHEAX_EREADONLY);
	cheax_ft(c, pad);

	c->user_error_names.array[code - CHEAX_EUSER0] = store_name;
	++c->user_error_names.len;

	return code;
pad:
	return -1;
}

int
cheax_find_error_code(CHEAX *c, const char *name)
{
	if (name == NULL) {
		cheax_throwf(c, CHEAX_EAPI, "find_error_code(): `name' cannot be NULL");
		return -1;
	}

	for (size_t i = 0; i < c->user_error_names.len; ++i)
		if (0 == strcmp(name, c->user_error_names.array[i]))
			return i + CHEAX_EUSER0;

	size_t num_bltn = sizeof(bltn_error_names) / sizeof(bltn_error_names[0]);
	for (size_t i = 0; i < num_bltn; ++i)
		if (0 == strcmp(name, bltn_error_names[i].name))
			return bltn_error_names[i].code;

	return -1;
}

int
bt_init(CHEAX *c, size_t limit)
{
	c->bt.array = NULL;
	c->bt.len = c->bt.limit = 0;
	c->bt.last_call = NULL;
	c->bt.truncated = false;
	return bt_limit(c, limit);
}

int
bt_limit(CHEAX *c, size_t limit)
{
	if (c->bt.len > 0) {
		cheax_throwf(c, CHEAX_EEVAL, "bt_limit(): backtrace limit locked");
		return -1;
	}

	cheax_free(c, c->bt.array);
	c->bt.array = cheax_calloc(c, limit, sizeof(c->bt.array[0]));
	if (c->bt.array == NULL) {
		c->bt.limit = 0;
		return -1;
	}

	c->bt.limit = limit;
	return 0;
}

void
cheax_add_bt(CHEAX *c)
{
	if (cheax_errno(c) == 0) {
		cheax_throwf(c, CHEAX_EAPI, "add_bt(): no error has been thrown");
		return;
	}

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

struct chx_value *
bt_wrap(CHEAX *c, struct chx_value *v)
{
	return (cheax_errno(c) == 0) ? v : (cheax_add_bt(c), NULL);
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
	if (unpack(c, args, "x![s ]?", &code, &msg) < 0)
		return NULL;

	if (code == 0)
		cheax_throwf(c, CHEAX_EVALUE, "cannot throw ENOERR");
	else
		cheax_throw(c, code, msg);
	return bt_wrap(c, NULL);
}

static int
validate_catch_blocks(CHEAX *c, struct chx_list *catch_blocks, struct chx_list **finally_block)
{
	for (struct chx_list *cb = catch_blocks; cb != NULL; cb = cb->next) {
		struct chx_value *cb_value = cb->value;
		if (cheax_type_of(cb_value) != CHEAX_LIST) {
			cheax_throwf(c, CHEAX_ETYPE, "catch/finally blocks must be s-expressions");
			cheax_add_bt(c);
			return -1;
		}

		struct chx_list *cb_list = (struct chx_list *)cb_value;
		bool is_id = cheax_type_of(cb_list->value) == CHEAX_ID;

		struct chx_id *keyword = (struct chx_id *)cb_list->value;
		if (is_id && 0 == strcmp("catch", keyword->id)) {
			if (cb_list->next == NULL || cb_list->next->next == NULL) {
				cheax_throwf(c, CHEAX_EMATCH, "expected at least two arguments");
				cheax_add_bt(c);
				return -1;
			}
		} else if (is_id && 0 == strcmp("finally", keyword->id)) {
			if (cb->next != NULL) {
				cheax_throwf(c, CHEAX_EVALUE, "unexpected values after finally block");
				cheax_add_bt(c);
				return -1;
			}

			*finally_block = cb;
		} else {
			cheax_throwf(c, CHEAX_EMATCH, "expected `catch' or `finally' keyword");
			cheax_add_bt(c);
			return -1;
		}
	}

	return 0;
}

static struct chx_list *
match_catch(CHEAX *c,
            struct chx_list *catch_blocks,
            struct chx_list *finally_block,
            int active_errno)
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

		if (cheax_type_of(errcodes) != CHEAX_LIST) {
			errcodes = &cheax_list(c, errcodes, NULL)->base;
			cheax_ft(c, pad);
		}

		struct chx_list *enode;
		for (enode = (struct chx_list *)errcodes; enode != NULL; enode = enode->next) {
			struct chx_value *code = enode->value;
			if (cheax_type_of(code) != CHEAX_ERRORCODE) {
				cheax_throwf(c, CHEAX_ETYPE, "expected error code or list thereof");
				cheax_add_bt(c);
				return NULL;
			}

			if (((struct chx_int *)code)->value == active_errno)
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

	cheax_clear_errno(c);

	for (struct chx_list *cons = run_blocks; cons != NULL; cons = cons->next) {
		retval = cheax_eval(c, cons->value);
		cheax_ft(c, pad); /* new error thrown */
	}

pad:
	cheax_unref(c, run_blocks, run_blocks_ref);
	return retval;
}

static void
run_finally(CHEAX *c, struct chx_list *finally_block)
{
	int active_errno = c->error.code;
	struct chx_string *active_msg = c->error.msg;
	chx_ref active_msg_ref = cheax_ref(c, active_msg);

	/* Semi-clear-errno state; see warning comment in bltn_try(). */
	c->error.code = 0;
	c->error.msg = NULL;

	cheax_push_env(c);
	cheax_ft(c, pad2);

	/* types checked before, so this should all be safe */
	struct chx_list *fb = (struct chx_list *)finally_block->value;
	for (struct chx_list *cons = fb->next; cons != NULL; cons = cons->next) {
		cheax_eval(c, cons->value);
		cheax_ft(c, pad);
	}

	c->error.code = active_errno;
	c->error.msg = active_msg;
pad:
	cheax_pop_env(c);
pad2:
	cheax_unref(c, active_msg, active_msg_ref);
	return;
}

static struct chx_value *
bltn_try(CHEAX *c, struct chx_list *args, void *info)
{
	if (args == NULL) {
		cheax_throwf(c, CHEAX_EMATCH, "expected at least two arguments");
		return bt_wrap(c, NULL);
	}

	struct chx_value *block = args->value;
	struct chx_list *catch_blocks = args->next;
	if (catch_blocks == NULL) {
		cheax_throwf(c, CHEAX_EMATCH, "expected at least one catch/finally block");
		return bt_wrap(c, NULL);
	}

	/* The item such that for the final catch block cb, we have
	 * cb->next == finally_block */
	struct chx_list *finally_block = NULL;

	if (validate_catch_blocks(c, catch_blocks, &finally_block) < 0)
		return NULL;

	struct chx_value *retval = NULL;

	cheax_push_env(c);
	cheax_ft(c, pad2);

	retval = cheax_eval(c, block);

	cheax_pop_env(c);

	if (cheax_errno(c) != 0) {
		/*
		 * We set errno and errmsg here rather than in run_catch(),
		 * to allow (catch errno ...), which matches any error code.
		 */
		int active_errno = c->error.code;

		/* protected against gc deletion by declaring it as a
		 * symbol later on */
		struct chx_string *active_msg = c->error.msg;

		/*
		 * We're now running a special semi-clear-errno state, where
		 * cheax_errno(c) == 0, but there might still be an active
		 * backtrace. Proceed with caution.
		 */
		c->error.code = 0;
		c->error.msg = NULL;

		cheax_push_env(c);
		cheax_ft(c, pad2);

		cheax_def(c, "errno",  &errorcode(c, active_errno)->base, CHEAX_READONLY);
		cheax_ft(c, pad);
		cheax_def(c, "errmsg", &active_msg->base, CHEAX_READONLY);
		cheax_ft(c, pad);

		struct chx_list *match = match_catch(c, catch_blocks, finally_block, active_errno);
		if (match == NULL) {
			if (cheax_errno(c) == 0) {
				/* error falls through */
				c->error.code = active_errno;
				c->error.msg = active_msg;
			}
		} else {
			retval = run_catch(c, match);
		}

pad:
		cheax_pop_env(c);
	}

	if (finally_block != NULL) {
		chx_ref retval_ref = cheax_ref(c, retval);
		run_finally(c, finally_block);
		cheax_unref(c, retval, retval_ref);
	}
pad2:
	return retval;
}

static struct chx_value *
bltn_new_error_code(CHEAX *c, struct chx_list *args, void *info)
{
	const char *errname;
	if (unpack(c, args, "N!", &errname) < 0)
		return NULL;

	if (cheax_find_error_code(c, errname) >= 0)
		cheax_throwf(c, CHEAX_EEXIST, "error with name %s already exists", errname);
	else
		cheax_new_error_code(c, errname);
	return bt_wrap(c, NULL);
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
