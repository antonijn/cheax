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
#include <stdio.h>
#include <string.h>

#include "api.h"
#include "feat.h"
#include "io.h"
#include "strm.h"
#include "unpack.h"

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

void
load_io_feature(CHEAX *c, int bits)
{
	if (has_flag(bits, FILE_IO)) {
		cheax_defmacro(c, "fopen", bltn_fopen, NULL);
		cheax_defmacro(c, "fclose", bltn_fclose, NULL);
	}

	if (has_flag(bits, EXPOSE_STDIN)) {
		cheax_var(c, "stdin",
		          &cheax_user_ptr(c, stdin,  c->fhandle_type)->base,
		          CHEAX_READONLY);
	}

	if (has_flag(bits, EXPOSE_STDOUT)) {
		cheax_var(c, "stdout",
		          &cheax_user_ptr(c, stdout, c->fhandle_type)->base,
		          CHEAX_READONLY);
	}

	if (has_flag(bits, EXPOSE_STDERR)) {
		cheax_var(c, "stderr",
		          &cheax_user_ptr(c, stderr, c->fhandle_type)->base,
		          CHEAX_READONLY);
	}
}

void
export_io_bltns(CHEAX *c)
{
	c->fhandle_type = cheax_new_type(c, "FileHandle", CHEAX_USER_PTR);

	cheax_defmacro(c, "read-from",     bltn_read_from,     NULL);
	cheax_defmacro(c, "print-to",      bltn_print_to,      NULL);
	cheax_defmacro(c, "put-to",        bltn_put_to,        NULL);
	cheax_defmacro(c, "get-byte-from", bltn_get_byte_from, NULL);
	cheax_defmacro(c, "get-line-from", bltn_get_line_from, NULL);
}
