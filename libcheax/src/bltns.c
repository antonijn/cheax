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
	};

	int nbltns = sizeof(bltns) / sizeof(bltns[0]);
	for (int i = 0; i < nbltns; ++i)
		cheax_defmacro(c, bltns[i].name, bltns[i].fn, NULL);

	cheax_defsym(c, "features",      get_features,      NULL, NULL, NULL);

	export_arith_bltns(c);
	export_core_bltns(c);
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
