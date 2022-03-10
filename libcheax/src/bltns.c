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
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "api.h"
#include "config.h"
#include "setup.h"
#include "format.h"
#include "gc.h"
#include "strm.h"
#include "unpack.h"

/*
 *   __ _ _        _
 *  / _(_) | ___  (_) ___
 * | |_| | |/ _ \ | |/ _ \
 * |  _| | |  __/ | | (_) |
 * |_| |_|_|\___| |_|\___/
 *
 */

static struct chx_value *
bltn_fopen(CHEAX *c, struct chx_list *args, void *info)
{
	struct chx_string *fname_val, *mode_val;
	if (unpack(c, "fopen", args, "ss", &fname_val, &mode_val) < 0)
		return NULL;

	char *fname;
	fname = cheax_malloc(c, fname_val->len + 1);
	memcpy(fname, fname_val->value, fname_val->len);
	fname[fname_val->len] = '\0';

	char *mode;
	mode = cheax_malloc(c, mode_val->len + 1);
	memcpy(mode, mode_val->value, mode_val->len);
	mode[mode_val->len] = '\0';

	FILE *f = fopen(fname, mode);

	cheax_free(c, fname);
	cheax_free(c, mode);

	if (f == NULL) {
		/* TODO inspect errno */
		cry(c, "fopen", CHEAX_EIO, "error opening file");
		return NULL;
	}

	return &cheax_user_ptr(c, f, c->fhandle_type)->base;

}

static struct chx_value *
bltn_fclose(CHEAX *c, struct chx_list *args, void *info)
{
	FILE *f;
	if (0 == unpack(c, "fclose", args, "f!", &f))
		fclose(f);
	return NULL;
}

static struct chx_value *
bltn_read_from(CHEAX *c, struct chx_list *args, void *info)
{
	FILE *f;
	return (0 == unpack(c, "read-from", args, "f!", &f))
	     ? cheax_read(c, f)
	     : NULL;
}

static struct chx_value *
bltn_print_to(CHEAX *c, struct chx_list *args, void *info)
{
	FILE *f;
	struct chx_value *v;
	if (0 == unpack(c, "print-to", args, "f!.", &f, &v)) {
		cheax_print(c, f, v);
		fputc('\n', f);
	}
	return NULL;
}

static struct chx_value *
bltn_put_to(CHEAX *c, struct chx_list *args, void *info)
{
	FILE *f;
	struct chx_string *s;
	if (0 == unpack(c, "put-to", args, "f!s", &f, &s))
		fwrite(s->value, 1, s->len, f);
	return NULL;
}

static struct chx_value *
bltn_get_byte_from(CHEAX *c, struct chx_list *args, void *info)
{
	FILE *f;
	if (unpack(c, "get-byte-from", args, "f!", &f) < 0)
		return NULL;

	int ch = fgetc(f);
	return (ch == EOF) ? NULL : &cheax_int(c, ch)->base;
}
static struct chx_value *
bltn_get_line_from(CHEAX *c, struct chx_list *args, void *info)
{
	/*
	 * This could all be implemented in the prelude, but for the
	 * sake of performance it's done here.
	 */
	FILE *f;
	if (unpack(c, "get-line-from", args, "f!", &f) < 0)
		return NULL;

	struct sostrm ss;
	sostrm_init(&ss, c);

	int ch;
	while ((ch = fgetc(f)) != EOF) {
		if (ostrm_putc(&ss.strm, ch) == -1) {
			cheax_free(c, ss.buf);
			return NULL;
		}

		if (ch == '\n')
			break;
	}

	struct chx_string *res = cheax_nstring(c, ss.buf, ss.idx);
	cheax_free(c, ss.buf);
	return &res->base;
}

/*
 *      _        _                                     _
 *  ___| |_ _ __(_)_ __   __ _   _ __ ___   __ _ _ __ (_)_ __
 * / __| __| '__| | '_ \ / _` | | '_ ` _ \ / _` | '_ \| | '_ \
 * \__ \ |_| |  | | | | | (_| | | | | | | | (_| | | | | | |_) |
 * |___/\__|_|  |_|_| |_|\__, | |_| |_| |_|\__,_|_| |_|_| .__(_)
 *                       |___/                          |_|
 */

static struct chx_value *
bltn_format(CHEAX *c, struct chx_list *args, void *info)
{
	struct chx_string *fmt;
	struct chx_list *lst;
	return (0 == unpack(c, "format", args, "s.*", &fmt, &lst))
	     ? format(c, fmt, lst)
	     : NULL;
}

static struct chx_value *
bltn_strbytes(CHEAX *c, struct chx_list *args, void *info)
{
	struct chx_string *str;
	if (unpack(c, "strbytes", args, "s", &str) < 0)
		return NULL;

	struct chx_list *bytes = NULL;
	for (int i = (int)str->len - 1; i >= 0; --i)
		bytes = cheax_list(c, &cheax_int(c, (unsigned char)str->value[i])->base, bytes);
	return &bytes->base;
}

static struct chx_value *
bltn_strsize(CHEAX *c, struct chx_list *args, void *info)
{
	struct chx_string *str;
	return (0 == unpack(c, "strsize", args, "s", &str))
	     ? &cheax_int(c, (int)str->len)->base
	     : NULL;
}

static struct chx_value *
bltn_substr(CHEAX *c, struct chx_list *args, void *info)
{
	struct chx_string *str;
	int pos, len = 0;
	struct chx_int *len_or_nil;
	if (unpack(c, "substr", args, "si!i?", &str, &pos, &len_or_nil) < 0)
		return NULL;

	if (len_or_nil != NULL)
		len = len_or_nil->value;
	else if (pos >= 0 && (size_t)pos <= str->len)
		len = str->len - (size_t)pos;

	if (pos < 0 || len < 0) {
		cry(c, "substr", CHEAX_EVALUE, "expected positive integer");
		return NULL;
	}

	return &cheax_substr(c, str, pos, len)->base;
}

/*
 *                             _                     _ _ _
 *   ___ _ __ _ __ ___  _ __  | |__   __ _ _ __   __| | (_)_ __   __ _
 *  / _ \ '__| '__/ _ \| '__| | '_ \ / _` | '_ \ / _` | | | '_ \ / _` |
 * |  __/ |  | | | (_) | |    | | | | (_| | | | | (_| | | | | | | (_| |
 *  \___|_|  |_|  \___/|_|    |_| |_|\__,_|_| |_|\__,_|_|_|_| |_|\__, |
 *                                                               |___/
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
	if (cheax_push_env(c) == NULL)
		return;

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

	if (cheax_push_env(c) == NULL)
		return NULL;
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
		struct chx_int *code = cheax_int(c, cheax_errno(c));
		set_type(&code->base, CHEAX_ERRORCODE);
		cheax_var(c, "errno",  &code->base,         CHEAX_READONLY);
		cheax_var(c, "errmsg", &c->error.msg->base, CHEAX_READONLY);

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

/*
 *            _ _
 *   _____  _(_) |_
 *  / _ \ \/ / | __|
 * |  __/>  <| | |_
 *  \___/_/\_\_|\__|
 *
 */

static struct chx_value *
bltn_exit(CHEAX *c, struct chx_list *args, void *info)
{
	struct chx_value *code_val;
	if (unpack(c, "exit", args, "i?", &code_val) < 0)
		return NULL;

	exit((code_val == NULL) ? 0 : ((struct chx_int *)code_val)->value);
}

#ifndef USE_BOEHM_GC

/*
 *
 *   __ _  ___
 *  / _` |/ __|
 * | (_| | (__
 *  \__, |\___|
 *  |___/
 */

static struct chx_value *
bltn_gc(CHEAX *c, struct chx_list *args, void *info)
{
	if (unpack(c, "gc", args, "") < 0)
		return NULL;

	static struct chx_id mem = { { CHEAX_ID | NO_GC_BIT }, "mem" },
			      to = { { CHEAX_ID | NO_GC_BIT }, "->" },
			     obj = { { CHEAX_ID | NO_GC_BIT }, "obj" };

	int mem_i = c->gc.all_mem, obj_i = c->gc.num_objects;
	cheax_force_gc(c);
	int mem_f = c->gc.all_mem, obj_f = c->gc.num_objects;

	return &cheax_list(c, &mem.base,
		cheax_list(c, &cheax_int(c, mem_i)->base,
		cheax_list(c, &to.base,
		cheax_list(c, &cheax_int(c, mem_f)->base,
		cheax_list(c, &obj.base,
		cheax_list(c, &cheax_int(c, obj_i)->base,
		cheax_list(c, &to.base,
		cheax_list(c, &cheax_int(c, obj_f)->base, NULL))))))))->base;
}
static struct chx_value *
bltn_get_used_memory(CHEAX *c, struct chx_list *args, void *info)
{
	return (0 == unpack(c, "get-used-memory", args, ""))
	     ? &cheax_int(c, c->gc.all_mem)->base
	     : NULL;
}

#endif

/*
 *                            _       __
 *  ___ _   _ _ __ ___     __| | ___ / _|
 * / __| | | | '_ ` _ \   / _` |/ _ \ |_
 * \__ \ |_| | | | | | | | (_| |  __/  _|
 * |___/\__, |_| |_| |_|  \__,_|\___|_|
 *      |___/
 */

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

	struct chx_func *res = gcol_alloc(c, sizeof(struct chx_func), CHEAX_FUNC);
	if (res != NULL) {
		res->args = getset_args;
		res->body = args;
		res->lexenv = c->env;
	}
	*out = res;
}

static struct chx_value *
bltn_defget(CHEAX *c, struct chx_list *args, void *info)
{
	struct defsym_info *dinfo = info;
	defgetset(c, "defget", NULL, args, dinfo, &dinfo->get);
	return NULL;
}
static struct chx_value *
bltn_defset(CHEAX *c, struct chx_list *args, void *info)
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
	struct chx_list sexpr = { { CHEAX_LIST | NO_GC_BIT }, &info->get->base, NULL };
	return cheax_eval(c, &sexpr.base);
}
static void
defsym_set(CHEAX *c, struct chx_sym *sym, struct chx_value *value)
{
	struct defsym_info *info = sym->user_info;
	struct chx_quote arg  = { { CHEAX_QUOTE | NO_GC_BIT }, value };
	struct chx_list args  = { { CHEAX_LIST | NO_GC_BIT }, &arg.base,        NULL  };
	struct chx_list sexpr = { { CHEAX_LIST | NO_GC_BIT }, &info->set->base, &args };
	cheax_eval(c, &sexpr.base);
}
static void
defsym_finalizer(CHEAX *c, struct chx_sym *sym)
{
	cheax_free(c, sym->user_info);
}

static struct chx_value *
bltn_defsym(CHEAX *c, struct chx_list *args, void *info)
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

	struct defsym_info *dinfo = cheax_malloc(c, sizeof(struct defsym_info));
	if (dinfo == NULL)
		return NULL;
	dinfo->get = dinfo->set = NULL;

	struct chx_ext_func *defget, *defset;
	defget = cheax_ext_func(c, "defget", bltn_defget, dinfo);
	defset = cheax_ext_func(c, "defset", bltn_defset, dinfo);
	cheax_ft(c, err_pad); /* alloc failure */

	if (cheax_push_env(c) == NULL)
		goto err_pad;

	cheax_var(c, "defget", &defget->base, CHEAX_READONLY);
	cheax_var(c, "defset", &defset->base, CHEAX_READONLY);
	cheax_ft(c, pad); /* alloc failure */

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
	cheax_free(c, dinfo);
	return NULL;
}

static struct chx_value *
bltn_def(CHEAX *c, struct chx_list *args, void *info)
{
	struct chx_value *idval, *setto;
	if (0 == unpack(c, "def", args, "_.", &idval, &setto)
	 && !cheax_match(c, idval, setto, CHEAX_READONLY))
	{
		cry(c, "def", CHEAX_EMATCH, "invalid pattern");
	}
	return NULL;
}
static struct chx_value *
bltn_var(CHEAX *c, struct chx_list *args, void *info)
{
	struct chx_value *idval, *setto;
	if (0 == unpack(c, "var", args, "_.?", &idval, &setto)
	 && !cheax_match(c, idval, setto, 0))
	{
		cry(c, "var", CHEAX_EMATCH, "invalid pattern");
	}
	return NULL;
}

static struct chx_value *
bltn_let(CHEAX *c, struct chx_list *args, void *info)
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

	if (cheax_push_env(c) == NULL)
		return NULL;

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
	for (struct chx_list *cons = args->next; cons != NULL; cons = cons->next) {
		retval = cheax_eval(c, cons->value);
		cheax_ft(c, pad);
	}

	res = retval;
pad:
	cheax_pop_env(c);
	return res;
}

static struct chx_value *
bltn_set(CHEAX *c, struct chx_list *args, void *info)
{
	const char *id;
	struct chx_value *setto;
	if (0 == unpack(c, "set", args, "N!.", &id, &setto))
		cheax_set(c, id, setto);
	return NULL;
}

/*
 *                  _
 * __   ____ _ _ __(_) ___  _   _ ___
 * \ \ / / _` | '__| |/ _ \| | | / __|
 *  \ V / (_| | |  | | (_) | |_| \__ \
 *   \_/ \__,_|_|  |_|\___/ \__,_|___/
 *
 */

static struct chx_value *
bltn_env(CHEAX *c, struct chx_list *args, void *info)
{
	return (0 == unpack(c, "env", args, ""))
	     ? &c->env->base
	     : NULL;
}

static struct chx_list *
prepend(CHEAX *c, struct chx_list *args)
{
	if (args->next != NULL) {
		struct chx_value *head = cheax_eval(c, args->value);
		cheax_ft(c, pad);
		chx_ref head_ref = cheax_ref(c, head);
		struct chx_list *tail = prepend(c, args->next);
		cheax_unref(c, head, head_ref);
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
bltn_prepend(CHEAX *c, struct chx_list *args, void *info)
{
	if (args == NULL) {
		cry(c, ":", CHEAX_EMATCH, "expected at least one argument");
		return NULL;
	}

	return &prepend(c, args)->base;
}

static struct chx_value *
bltn_type_of(CHEAX *c, struct chx_list *args, void *info)
{
	struct chx_value *val;
	return (0 == unpack(c, "type-of", args, ".", &val))
	     ? set_type(&cheax_int(c, cheax_type_of(val))->base, CHEAX_TYPECODE)
	     : NULL;
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

	struct chx_func *res = gcol_alloc(c, sizeof(struct chx_func), type);
	if (res != NULL) {
		res->args = arg_list;
		res->body = body;
		res->lexenv = c->env;
	}
	return &res->base;
}

static struct chx_value *
bltn_fn(CHEAX *c, struct chx_list *args, void *info)
{
	return create_func(c, "fn", args, CHEAX_FUNC);
}
static struct chx_value *
bltn_macro(CHEAX *c, struct chx_list *args, void *info)
{
	return create_func(c, "macro", args, CHEAX_MACRO);
}

static struct chx_value *
bltn_eval(CHEAX *c, struct chx_list *args, void *info)
{
	struct chx_value *arg;
	return (0 == unpack(c, "eval", args, ".", &arg))
	     ? cheax_eval(c, arg)
	     : NULL;
}

static struct chx_value *
bltn_case(CHEAX *c, struct chx_list *args, void *info)
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

		if (cheax_push_env(c) == NULL)
			goto pad;

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

	cry(c, "case", CHEAX_EMATCH, "non-exhaustive pattern");
pad:
	return NULL;
}

/*
 *             _ _   _                    _   _
 *   __ _ _ __(_) |_| |__  _ __ ___   ___| |_(_) ___
 *  / _` | '__| | __| '_ \| '_ ` _ \ / _ \ __| |/ __|
 * | (_| | |  | | |_| | | | | | | | |  __/ |_| | (__
 *  \__,_|_|  |_|\__|_| |_|_| |_| |_|\___|\__|_|\___|
 *
 */

static struct chx_value *
do_aop(CHEAX *c,
       const char *name,
       struct chx_list *args,
       int (*iop)(CHEAX *, int, int),
       double (*fop)(CHEAX *, double, double))
{
	struct chx_value *l, *r;
	if (unpack(c, name, args, "[id][id]", &l, &r) < 0)
		return NULL;

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
iop_add(CHEAX *c, int a, int b)
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
iop_sub(CHEAX *c, int a, int b)
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
iop_mul(CHEAX *c, int a, int b)
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
iop_div(CHEAX *c, int a, int b)
{
	if (((a == -1) && (b == INT_MIN))
	 || ((b == -1) && (a == INT_MIN)))
	{
		cry(c, "/", CHEAX_EOVERFLOW, "integer overflow");
		return 0;
	}

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
iop_mod(CHEAX *c, int a, int b)
{
	if (((a == -1) && (b == INT_MIN))
	 || ((b == -1) && (a == INT_MIN)))
	{
		cry(c, "%", CHEAX_EOVERFLOW, "integer overflow");
		return 0;
	}

	if (b == 0) {
		cry(c, "%", CHEAX_EDIVZERO, "division by zero");
		return 0;
	}

	return a % b;
}

static struct chx_value *
bltn_add(CHEAX *c, struct chx_list *args, void *info)
{
	return do_aop(c, "+", args, iop_add, fop_add);
}
static struct chx_value *
bltn_sub(CHEAX *c, struct chx_list *args, void *info)
{
	return do_aop(c, "-", args, iop_sub, fop_sub);
}
static struct chx_value *
bltn_mul(CHEAX *c, struct chx_list *args, void *info)
{
	return do_aop(c, "*", args, iop_mul, fop_mul);
}
static struct chx_value *
bltn_div(CHEAX *c, struct chx_list *args, void *info)
{
	return do_aop(c, "/", args, iop_div, fop_div);
}
static struct chx_value *
bltn_mod(CHEAX *c, struct chx_list *args, void *info)
{
	return do_aop(c, "%", args, iop_mod, NULL);
}

static struct chx_value *
bltn_eq(CHEAX *c, struct chx_list *args, void *info)
{
	struct chx_value *l, *r;
	return (0 == unpack(c, "=", args, "..", &l, &r))
	     ? &cheax_bool(c, cheax_eq(c, l, r))->base
	     : NULL;
}
static struct chx_value *
bltn_ne(CHEAX *c, struct chx_list *args, void *info)
{
	struct chx_value *l, *r;
	return (0 == unpack(c, "!=", args, "..", &l, &r))
	     ? &cheax_bool(c, !cheax_eq(c, l, r))->base
	     : NULL;
}

static struct chx_value *
do_cmp(CHEAX *c,
       const char *name,
       struct chx_list *args,
       bool lt, bool eq, bool gt)
{
	struct chx_value *l, *r;
	if (unpack(c, name, args, "[id][id]", &l, &r) < 0)
		return NULL;

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
bltn_lt(CHEAX *c, struct chx_list *args, void *info)
{
	return do_cmp(c, "<", args, 1, 0, 0);
}
static struct chx_value *
bltn_le(CHEAX *c, struct chx_list *args, void *info)
{
	return do_cmp(c, "<=", args, 1, 1, 0);
}
static struct chx_value *
bltn_gt(CHEAX *c, struct chx_list *args, void *info)
{
	return do_cmp(c, ">", args, 0, 0, 1);
}
static struct chx_value *
bltn_ge(CHEAX *c, struct chx_list *args, void *info)
{
	return do_cmp(c, ">=", args, 0, 1, 1);
}

static struct chx_value *
get_cheax_version(CHEAX *c, struct chx_sym *sym)
{
	static struct chx_string res = {
		{ CHEAX_STRING | NO_GC_BIT }, VERSION_STRING, sizeof(VERSION_STRING) - 1, &res
	};
	return &res.base;
}

static struct chx_value *get_features(CHEAX *c, struct chx_sym *sym);

void
export_builtins(CHEAX *c)
{
	c->fhandle_type = cheax_new_type(c, "FileHandle", CHEAX_USER_PTR);

	struct { const char *name; chx_func_ptr fn; } bltns[] = {
		{ "read-from",       bltn_read_from       },
		{ "print-to",        bltn_print_to        },
		{ "put-to",          bltn_put_to          },
		{ "get-byte-from",   bltn_get_byte_from   },
		{ "get-line-from",   bltn_get_line_from   },

		{ "format",          bltn_format          },
		{ "strbytes",        bltn_strbytes        },
		{ "strsize",         bltn_strsize         },
		{ "substr",          bltn_substr          },

		{ "throw",           bltn_throw           },
		{ "try",             bltn_try             },
		{ "new-error-code",  bltn_new_error_code  },

		{ "defsym",          bltn_defsym          },
		{ "var",             bltn_var             },
		{ "def",             bltn_def             },
		{ "let",             bltn_let             },
		{ "set",             bltn_set             },

		{ "env",             bltn_env             },
		{ ":",               bltn_prepend         },
		{ "type-of",         bltn_type_of         },
		{ "fn",              bltn_fn              },
		{ "macro",           bltn_macro           },
		{ "eval",            bltn_eval            },
		{ "case",            bltn_case            },

		{ "+",               bltn_add             },
		{ "-",               bltn_sub             },
		{ "*",               bltn_mul             },
		{ "/",               bltn_div             },
		{ "%",               bltn_mod             },
		{ "=",               bltn_eq              },
		{ "!=",              bltn_ne              },
		{ "<",               bltn_lt              },
		{ "<=",              bltn_le              },
		{ ">",               bltn_gt              },
		{ ">=",              bltn_ge              },
	};

	int nbltns = sizeof(bltns) / sizeof(bltns[0]);
	for (int i = 0; i < nbltns; ++i)
		cheax_defmacro(c, bltns[i].name, bltns[i].fn, NULL);

	cheax_defsym(c, "cheax-version", get_cheax_version, NULL, NULL, NULL);
	cheax_defsym(c, "features",      get_features,      NULL, NULL, NULL);
}

/* sorted asciibetically for use in bsearch() */
static const struct nfeat { const char *name; int feat; } named_feats[] = {
	{"all",             ALL_FEATURES    },
	{"exit",            EXIT_BUILTIN    },
	{"file-io",         FILE_IO         },
#ifndef USE_BOEHM_GC
	{"gc",              GC_BUILTIN      },
#endif
	{"stderr",          EXPOSE_STDERR   },
	{"stdin",           EXPOSE_STDIN    },
	{"stdio",           STDIO           },
	{"stdout",          EXPOSE_STDOUT   },
};

/* used in bsearch() */
static int
feature_compar(const char *key, const struct nfeat *nf)
{
	return strcmp(key, nf->name);
}

static struct chx_list *
feature_list(CHEAX *c, struct chx_list *base)
{
	struct chx_list *list = base;
	int len = sizeof(named_feats) / sizeof(named_feats[0]);
	for (int i = len - 1; i >= 0; --i)
		if (has_flag(c->features, named_feats[i].feat))
			list = cheax_list(c, &cheax_string(c, named_feats[i].name)->base, list);
	return list;
}

static struct chx_value *
get_features(CHEAX *c, struct chx_sym *sym)
{
	return &feature_list(c, config_feature_list(c, NULL))->base;
}

static int
find_feature(const char *feat)
{
	struct nfeat *res;
	res = bsearch(feat, named_feats,
	              sizeof(named_feats) / sizeof(named_feats[0]), sizeof(named_feats[0]),
	              (int (*)(const void *, const void *))feature_compar);
	return (res == NULL) ? 0 : res->feat;
}

int
cheax_load_feature(CHEAX *c, const char *feat)
{
	int feats = find_feature(feat) | find_config_feature(feat);
	if (feats == 0)
		return -1;

	/* newly set features */
	int nf = feats & ~c->features;

	if (has_flag(nf, FILE_IO)) {
		cheax_defmacro(c, "fopen", bltn_fopen, NULL);
		cheax_defmacro(c, "fclose", bltn_fclose, NULL);
	}

#ifndef USE_BOEHM_GC
	if (has_flag(nf, GC_BUILTIN)) {
		cheax_defmacro(c, "gc", bltn_gc, NULL);
		cheax_defmacro(c, "get-used-memory", bltn_get_used_memory, NULL);
	}
#endif

	if (has_flag(nf, EXIT_BUILTIN))
		cheax_defmacro(c, "exit", bltn_exit, NULL);

	if (has_flag(nf, EXPOSE_STDIN)) {
		cheax_var(c, "stdin",
		          &cheax_user_ptr(c, stdin,  c->fhandle_type)->base,
		          CHEAX_READONLY);
	}

	if (has_flag(nf, EXPOSE_STDOUT)) {
		cheax_var(c, "stdout",
		          &cheax_user_ptr(c, stdout, c->fhandle_type)->base,
		          CHEAX_READONLY);
	}

	if (has_flag(nf, EXPOSE_STDERR)) {
		cheax_var(c, "stderr",
		          &cheax_user_ptr(c, stderr, c->fhandle_type)->base,
		          CHEAX_READONLY);
	}

	load_config_features(c, nf);

	c->features |= nf;
	return 0;
}
