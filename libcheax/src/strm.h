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

#ifndef STRM_H
#define STRM_H

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "core.h"

/* output stream */
struct ostrm {
	void *info;
	int (*vprintf)(void *info, const char *frmt, va_list ap);
	/* can't use identifier putc; might be macro */
	int (*putchar)(void *info, int ch);
};

static inline int
ostrm_printf(struct ostrm *strm, const char *frmt, ...)
{
	va_list ap;
	va_start(ap, frmt);
	int res = strm->vprintf(strm->info, frmt, ap);
	va_end(ap);
	return res;
}
static inline int
ostrm_putc(struct ostrm *strm, int ch)
{
	return strm->putchar(strm->info, ch);
}

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
void ostrm_printi(struct ostrm *strm, int num, char pad_char, int field_width, char misc_spec);

/* string output stream */
struct sostrm {
	struct ostrm strm;

	CHEAX *c;
	char *buf;
	size_t idx, cap;
};

int sostrm_expand(struct sostrm *stream, size_t req_buf);
int sostrm_vprintf(void *info, const char *frmt, va_list ap);
int sostrm_putc(void *info, int ch);

static inline void
sostrm_init(struct sostrm *ss, CHEAX *c)
{
	ss->c = c;
	ss->buf = NULL;
	ss->idx = ss->cap = 0;

	ss->strm.info = ss;
	ss->strm.vprintf = sostrm_vprintf;
	ss->strm.putchar = sostrm_putc;
}

/* buffer (string-n) output stream.
 * or snowstorm, whichever you prefer. */
struct snostrm {
	struct ostrm strm;

	char *buf;
	size_t idx, cap;
};

int snostrm_vprintf(void *info, const char *frmt, va_list ap);
int snostrm_putc(void *info, int ch);

static inline void
snostrm_init(struct snostrm *ss, char *buf, size_t cap)
{
	ss->buf = memset(buf, 0, cap);
	ss->idx = 0;
	ss->cap = cap;

	ss->strm.info = ss;
	ss->strm.vprintf = snostrm_vprintf;
	ss->strm.putchar = snostrm_putc;
}

/* file output stream */
struct fostrm {
	struct ostrm strm;

	CHEAX *c;
	FILE *f;
};

int fostrm_vprintf(void *info, const char *frmt, va_list ap);
int fostrm_putc(void *info, int ch);

static inline void
fostrm_init(struct fostrm *fs, FILE *f, CHEAX *c)
{
	fs->c = c;
	fs->f = f;

	fs->strm.info = fs;
	fs->strm.vprintf = fostrm_vprintf;
	fs->strm.putchar = fostrm_putc;
}

/* input stream */
struct istrm {
	void *info;
	/* can't use identifier getc; might be macro */
	int (*getchar)(void *info);
};

static inline int
istrm_getc(struct istrm *strm)
{
	return strm->getchar(strm->info);
}

/* string input stream */
struct sistrm {
	struct istrm strm;

	const char *str;
	size_t idx, len;
};

int sistrm_getc(void *info);

static inline void
sistrm_initn(struct sistrm *ss, const char *str, size_t len)
{
	ss->str = str;
	ss->len = len;
	ss->idx = 0;

	ss->strm.info = ss;
	ss->strm.getchar = sistrm_getc;
}

static inline void
sistrm_init(struct sistrm *ss, const char *str)
{
	sistrm_initn(ss, str, strlen(str));
}

/* file input stream */
struct fistrm {
	struct istrm strm;

	CHEAX *c;
	FILE *f;
};

int fistrm_getc(void *info);

static inline void
fistrm_init(struct fistrm *fs, FILE *f, CHEAX *c)
{
	fs->c = c;
	fs->f = f;

	fs->strm.info = fs;
	fs->strm.getchar = fistrm_getc;
}

/* scanner */
struct scnr {
	int ch;
	struct istrm *strm;
	size_t max_lah, lah;
	int *lah_buf;

	int pos, line;
};

int scnr_adv(struct scnr *s);
int scnr_backup(struct scnr *s, int to);

static inline void
scnr_init(struct scnr *s, struct istrm *strm, size_t max_lah, int *lah_buf)
{
	s->ch = 0;
	s->strm = strm;
	s->max_lah = max_lah;
	s->lah_buf = lah_buf;
	s->lah = s->pos = 0;
	s->line = 1;
	scnr_adv(s);
}

#endif
