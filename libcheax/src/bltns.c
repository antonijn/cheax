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
#include "arith.h"
#include "config.h"
#include "err.h"
#include "eval.h"
#include "setup.h"
#include "format.h"
#include "gc.h"
#include "strm.h"
#include "sym.h"
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

/*
 *                  _
 * __   ____ _ _ __(_) ___  _   _ ___
 * \ \ / / _` | '__| |/ _ \| | | / __|
 *  \ V / (_| | |  | | (_) | |_| \__ \
 *   \_/ \__,_|_|  |_|\___/ \__,_|___/
 *
 */

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
		{ "read-from",     bltn_read_from     },
		{ "print-to",      bltn_print_to      },
		{ "put-to",        bltn_put_to        },
		{ "get-byte-from", bltn_get_byte_from },
		{ "get-line-from", bltn_get_line_from },

		{ "format",        bltn_format        },
		{ "strbytes",      bltn_strbytes      },
		{ "strsize",       bltn_strsize       },
		{ "substr",        bltn_substr        },

		{ ":",             bltn_prepend       },
		{ "type-of",       bltn_type_of       },
		{ "fn",            bltn_fn            },
		{ "macro",         bltn_macro         },
	};

	int nbltns = sizeof(bltns) / sizeof(bltns[0]);
	for (int i = 0; i < nbltns; ++i)
		cheax_defmacro(c, bltns[i].name, bltns[i].fn, NULL);

	cheax_defsym(c, "cheax-version", get_cheax_version, NULL, NULL, NULL);
	cheax_defsym(c, "features",      get_features,      NULL, NULL, NULL);

	export_arith_bltns(c);
	export_err_bltns(c);
	export_eval_bltns(c);
	export_sym_bltns(c);
}

/* sorted asciibetically for use in bsearch() */
static const struct nfeat { const char *name; int feat; } named_feats[] = {
	{"all",     ALL_FEATURES  },
	{"exit",    EXIT_BUILTIN  },
	{"file-io", FILE_IO       },
#ifndef USE_BOEHM_GC
	{"gc",      GC_BUILTIN    },
#endif
	{"stderr",  EXPOSE_STDERR },
	{"stdin",   EXPOSE_STDIN  },
	{"stdio",   STDIO         },
	{"stdout",  EXPOSE_STDOUT },
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
	load_gc_features(c, nf);

	c->features |= nf;
	return 0;
}
