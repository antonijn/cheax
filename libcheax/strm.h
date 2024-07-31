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

#ifndef STRM_H
#define STRM_H

#include <stdarg.h>
#include <stdio.h>

#include "core.h"

/* output stream */
struct ostrm {
	void *info;
	int (*vprintf)(void *info, const char *frmt, va_list ap);

	/* Optional; fallback is vprintf.
	 * Can't use identifier putc; might be macro */
	int (*sputc)(void *info, int ch);

	/* Optional: fallback is sputc. */
	int (*write)(void *info, const char *buf, size_t len);

	/* Optional */
	int (*expand)(void *info, size_t req);
};

int cheax_ostrm_printf_(struct ostrm *strm, const char *frmt, ...);
int cheax_ostrm_putc_(struct ostrm *strm, int ch);
int cheax_ostrm_write_(struct ostrm *strm, const char *buf, size_t len);
int cheax_ostrm_expand_(struct ostrm *strm, size_t extra);

/* Write unicode code point `cp' to output stream in UTF-8 encoding. */
void cheax_ostrm_put_utf_8(struct ostrm *ostr, unsigned cp);

/* Print integer `num' to ostrm `strm', padding it to length
 * `field_width' using padding character `pad_char' if necessary.
 * `misc_spec' can be:
 * 'X':   0xdeadbeef => "DEADBEEF" (uppercase hex)
 * 'x':   0xdeadbeef => "deadbeef" (lowercase hex)
 * 'o':   71         => "107"      (octal)
 * 'b':   123        => "1111011"  (binary)
 * other: 123        => "123"      (decimal)
 *
 * Why not just a printf() variant, I hear you ask. The problem with
 * printf() is that, depending on format specifier, it expects different
 * data types (unsigned int for 'x', for instance). This one just
 * handles int, and handles it well.
 */
void cheax_ostrm_printi_(struct ostrm *strm, chx_int num, char pad_char, int field_width, char misc_spec);

/* string output stream */
struct sostrm {
	struct ostrm strm;

	CHEAX *c;
	char *buf;
	size_t idx, cap;
};

void cheax_sostrm_init_(struct sostrm *ss, CHEAX *c);

/* buffer (string-n) output stream.
 * or snowstorm, whichever you prefer. */
struct snostrm {
	struct ostrm strm;

	char *buf;
	size_t idx, cap;
};

void cheax_snostrm_init_(struct snostrm *ss, char *buf, size_t cap);

/* file output stream */
struct fostrm {
	struct ostrm strm;

	CHEAX *c;
	FILE *f;
};

void cheax_fostrm_init_(struct fostrm *fs, FILE *f, CHEAX *c);

/* counting output stream */
struct costrm {
	struct ostrm strm, *base;
	size_t written;
};

void cheax_costrm_init_(struct costrm *cs, struct ostrm *base);

/* input stream */
struct istrm {
	void *info;
	/* can't use identifier getc; might be macro */
	int (*sgetc)(void *info);
};

int cheax_istrm_getc_(struct istrm *strm);

/* string input stream */
struct sistrm {
	struct istrm strm;

	const char *str;
	size_t idx, len;
};

void cheax_sistrm_initn_(struct sistrm *ss, const char *str, size_t len);
void cheax_sistrm_init_(struct sistrm *ss, const char *str);

/* file input stream */
struct fistrm {
	struct istrm strm;

	CHEAX *c;
	FILE *f;
};

void cheax_fistrm_init_(struct fistrm *fs, FILE *f, CHEAX *c);

/* scanner */
struct scnr {
	int ch;
	struct istrm *strm;
	size_t max_lah, lah;
	int *lah_buf;

	int pos, line;
};

void cheax_scnr_init_(struct scnr *s, struct istrm *strm, size_t max_lah, int *lah_buf, int line, int pos);
int cheax_scnr_adv_(struct scnr *s);
int cheax_scnr_backup_(struct scnr *s, int to);

#endif
