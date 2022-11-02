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
#include <stdarg.h>
#include <stdlib.h>

#include "core.h"
#include "err.h"
#include "print.h"
#include "unpack.h"

/* declare associative array of builtin error codes and their names */
CHEAX_BUILTIN_ERROR_NAMES(sf_error_names);

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

	int lo = 0, hi = sizeof(sf_error_names) / sizeof(sf_error_names[0]);
	while (lo <= hi) {
		int pivot = lo + (hi - lo) / 2;
		int pivot_code = sf_error_names[pivot].code;
		if (pivot_code == code)
			return sf_error_names[pivot].name;
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

	msg = cheax_string(c, sbuf).data.as_string;

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

	cheax_def(c, name, errorcode(code), CHEAX_EREADONLY);
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

	size_t num_bltn = sizeof(sf_error_names) / sizeof(sf_error_names[0]);
	for (size_t i = 0; i < num_bltn; ++i)
		if (0 == strcmp(name, sf_error_names[i].name))
			return sf_error_names[i].code;

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

static void
truncate_list_msg(CHEAX *c, char *dest, size_t size, struct chx_list *list)
{
	struct snostrm ss;
	strcpy(dest, "    ");
	snostrm_init(&ss, dest + 4, size - 4);
	ostrm_show(c, &ss.strm, cheax_list_value(list));
	strcpy(dest + size - 8, "...");
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
	struct bt_entry *ent = &c->bt.array[idx];
	memset(ent, 0, sizeof(struct bt_entry));

	struct chx_list *list_line1, *list_line2;

	struct loc_debug_info *info;
	struct chx_list *orig_form = get_orig_form(last_call);
	if (orig_form != NULL) {
		info = get_loc_debug_info(orig_form);
		list_line1 = orig_form;
		list_line2 = last_call;
	} else {
		info = get_loc_debug_info(last_call);
		list_line1 = last_call;
		list_line2 = NULL;
	}

	struct loc_debug_info no_info = { .file = "<filename unknown>", .pos = -1, .line = -1, };

	ent->info = (info != NULL) ? *info : no_info;

	truncate_list_msg(c, ent->line1, sizeof(ent->line1), list_line1);
	if (list_line2 != NULL)
		truncate_list_msg(c, ent->line2, sizeof(ent->line2), list_line2);
}

void
bt_add_tail_msg(CHEAX *c, int tail_lvls)
{
	if (c->bt.len >= c->bt.limit) {
		c->bt.truncated = true;
	} else {
		size_t idx = c->bt.len++;
		memset(&c->bt.array[idx], 0, sizeof(c->bt.array[idx]));
		sprintf(c->bt.array[idx].line1, "  ... tail calls (%d) ...", tail_lvls);
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
		struct bt_entry *ent = &c->bt.array[i - 1];

		if (ent->info.file != NULL) {
			fprintf(stderr, "  File \"%s\"", ent->info.file);
			if (ent->info.line > 0)
				fprintf(stderr, ", line %d", ent->info.line);
			fprintf(stderr, "\n");
		}

		fprintf(stderr, "%s\n", ent->line1);

		if (strlen(ent->line2) > 0) {
			fprintf(stderr, "   Expanded to:\n");
			fprintf(stderr, "%s\n", ent->line2);
		}
	}
}

struct chx_value
bt_wrap(CHEAX *c, struct chx_value v)
{
	return (cheax_errno(c) == 0) ? v : (cheax_add_bt(c), cheax_nil());
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
bltn_throw(CHEAX *c, struct chx_list *args, void *info)
{
	chx_int code;
	struct chx_value msg;
	if (unpack(c, args, "X[S-]?", &code, &msg) < 0)
		return cheax_nil();

	if (code == 0)
		cheax_throwf(c, CHEAX_EVALUE, "cannot throw ENOERR");
	else
		cheax_throw(c, code, cheax_is_nil(msg) ? NULL : msg.data.as_string);

	return bt_wrap(c, cheax_nil());
}

static int
validate_catch_blocks(CHEAX *c, struct chx_list *catch_blocks, struct chx_list **finally_block)
{
	for (struct chx_list *cb = catch_blocks; cb != NULL; cb = cb->next) {
		struct chx_value cb_value = cb->value;
		if (cb_value.type != CHEAX_LIST || cheax_is_nil(cb_value)) {
			cheax_throwf(c, CHEAX_ETYPE, "catch/finally blocks must be s-expressions");
			cheax_add_bt(c);
			return -1;
		}

		struct chx_list *cb_list = cb_value.data.as_list;
		struct chx_value keyword = cb_list->value;
		bool is_id = (keyword.type == CHEAX_ID);

		if (is_id && 0 == strcmp("catch", keyword.data.as_id->value)) {
			if (cb_list->next == NULL || cb_list->next->next == NULL) {
				cheax_throwf(c, CHEAX_EMATCH, "expected at least two arguments");
				cheax_add_bt(c);
				return -1;
			}
		} else if (is_id && 0 == strcmp("finally", keyword.data.as_id->value)) {
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
	for (struct chx_list *cb = catch_blocks; cb != finally_block; cb = cb->next) {
		/* these type casts should be safe, we did these checks
		 * beforehand */
		struct chx_list *cb_list = cb->value.data.as_list;

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
		struct chx_value errcodes = cheax_eval(c, cb_list->next->value);
		cheax_ft(c, pad);

		if (errcodes.type != CHEAX_LIST) {
			errcodes = cheax_list(c, errcodes, NULL);
			cheax_ft(c, pad);
		}

		struct chx_list *enode;
		for (enode = errcodes.data.as_list; enode != NULL; enode = enode->next) {
			struct chx_value code = enode->value;
			if (code.type != CHEAX_ERRORCODE) {
				cheax_throwf(c, CHEAX_ETYPE, "expected error code or list thereof");
				cheax_add_bt(c);
				return NULL;
			}

			if (code.data.as_int == active_errno)
				return cb_list;
		}
	}
pad:
	return NULL;
}

static struct chx_value
run_catch(CHEAX *c, struct chx_list *match)
{
	struct chx_value retval = cheax_nil();

	/* match, so run catch block code */
	struct chx_list *run_blocks = match->next->next;
	struct chx_value rbv = cheax_list_value(run_blocks);
	chx_ref run_blocks_ref = cheax_ref(c, rbv);

	cheax_clear_errno(c);

	for (struct chx_list *cons = run_blocks; cons != NULL; cons = cons->next) {
		retval = cheax_eval(c, cons->value);
		cheax_ft(c, pad); /* new error thrown */
	}

pad:
	cheax_unref(c, rbv, run_blocks_ref);
	return retval;
}

static void
run_finally(CHEAX *c, struct chx_list *finally_block)
{
	int active_errno = c->error.code;
	struct chx_string *active_msg = c->error.msg;
	struct chx_value amv = cheax_nil();
	chx_ref active_msg_ref = 0;
	if (active_msg != NULL) {
		amv = cheax_string_value(active_msg);
		active_msg_ref = cheax_ref(c, amv);
	}

	/* Semi-clear-errno state; see warning comment in sf_try(). */
	c->error.code = 0;
	c->error.msg = NULL;

	cheax_push_env(c);
	cheax_ft(c, pad2);

	/* types checked before, so this should all be safe */
	struct chx_list *fb = finally_block->value.data.as_list;
	for (struct chx_list *cons = fb->next; cons != NULL; cons = cons->next) {
		cheax_eval(c, cons->value);
		cheax_ft(c, pad);
	}

	c->error.code = active_errno;
	c->error.msg = active_msg;
pad:
	cheax_pop_env(c);
pad2:
	if (active_msg != NULL)
		cheax_unref(c, amv, active_msg_ref);
}

static int
sf_try(CHEAX *c,
       struct chx_list *args,
       void *info,
       struct chx_env *pop_stop,
       union chx_eval_out *out)
{
	if (args == NULL) {
		cheax_throwf(c, CHEAX_EMATCH, "expected at least two arguments");
		out->value = bt_wrap(c, cheax_nil());
		return CHEAX_VALUE_OUT;
	}

	struct chx_value block = args->value;
	struct chx_list *catch_blocks = args->next;
	if (catch_blocks == NULL) {
		cheax_throwf(c, CHEAX_EMATCH, "expected at least one catch/finally block");
		out->value = bt_wrap(c, cheax_nil());
		return CHEAX_VALUE_OUT;
	}

	/* The item such that for the final catch block cb, we have
	 * cb->next == finally_block */
	struct chx_list *finally_block = NULL;

	if (validate_catch_blocks(c, catch_blocks, &finally_block) < 0) {
		out->value = cheax_nil();
		return CHEAX_VALUE_OUT;
	}

	struct chx_value retval = cheax_nil();

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
		struct chx_value amv = cheax_nil();
		if (active_msg != NULL)
			amv = cheax_string_value(active_msg);

		/*
		 * We're now running a special semi-clear-errno state, where
		 * cheax_errno(c) == 0, but there might still be an active
		 * backtrace. Proceed with caution.
		 */
		c->error.code = 0;
		c->error.msg = NULL;

		cheax_push_env(c);
		cheax_ft(c, pad2);

		cheax_def(c, "errno",  errorcode(active_errno), CHEAX_READONLY);
		cheax_ft(c, pad);
		cheax_def(c, "errmsg", amv, CHEAX_READONLY);
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
	out->value = retval;
	return CHEAX_VALUE_OUT;
}

static struct chx_value
pp_sf_try(CHEAX *c, struct chx_list *args, void *info)
{
	/* (node
	 *   EXPR
	 *   (node
	 *     (node
	 *       LIT
	 *       (node EXPR (seq EXPR)))
	 *     (seq
	 *       (node
	 *         LIT
	 *         (node EXPR (seq EXPR))))))
	 */
	static const uint8_t ops[] = {
		PP_NODE | PP_ERR(0), PP_EXPR,

		/* first catch/finally block */
		PP_NODE | PP_ERR(1), PP_NODE | PP_ERR(2), PP_LIT,
		/* body of first catch/finally block */
		PP_NODE | PP_ERR(3), PP_EXPR, PP_SEQ, PP_EXPR,

		/* other catch/finally blocks */
		PP_SEQ, PP_NODE | PP_ERR(2), PP_LIT,
		/* body of first catch/finally blocks */
		PP_NODE | PP_ERR(3), PP_EXPR, PP_SEQ, PP_EXPR,
	};

	static const char *errors[] = {
		"expected value",
		"expected at least one catch/finally block",
		"expected try/catch keyword",
		"expected body",
	};

	return preproc_pattern(c, cheax_list_value(args), ops, errors);
}

static int
sf_new_error_code(CHEAX *c,
                  struct chx_list *args,
                  void *info,
                  struct chx_env *pop_stop,
                  union chx_eval_out *out)
{
	const char *errname;
	if (unpack(c, args, "N!", &errname) < 0) {
		out->value = cheax_nil();
		return CHEAX_VALUE_OUT;
	}

	if (cheax_find_error_code(c, errname) >= 0)
		cheax_throwf(c, CHEAX_EEXIST, "error with name %s already exists", errname);
	else
		cheax_new_error_code(c, errname);
	out->value = bt_wrap(c, cheax_nil());
	return CHEAX_VALUE_OUT;
}

static struct chx_value
pp_sf_new_error_code(CHEAX *c, struct chx_list *args, void *info)
{
	/* (node LIT NIL) */
	static const uint8_t ops[] = {
		PP_NODE | PP_ERR(0), PP_LIT, PP_NIL | PP_ERR(1),
	};

	static const char *errors[] = {
		"expected error code name",
		"unexpected values after error code name",
	};

	return preproc_pattern(c, cheax_list_value(args), ops, errors);
}

static void
export_error_names(CHEAX *c)
{
	int num_codes = sizeof(sf_error_names)
	              / sizeof(sf_error_names[0]);

	for (int i = 0; i < num_codes; ++i) {
		const char *name = sf_error_names[i].name;
		int code = sf_error_names[i].code;
		cheax_def(c, name, errorcode(code), CHEAX_READONLY);
	}
}

void
export_err_bltns(CHEAX *c)
{
	cheax_defun(c, "throw", bltn_throw, NULL);
	cheax_defsyntax(c, "try",            sf_try,            pp_sf_try,            NULL);
	cheax_defsyntax(c, "new-error-code", sf_new_error_code, pp_sf_new_error_code, NULL);

	export_error_names(c);
}
