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

#include <limits.h>
#include <string.h>

#include "err.h"
#include "loc.h"
#include "strm.h"

static int sostrm_vprintf(void *info, const char *frmt, va_list ap);
static int sostrm_putc(void *info, int ch);
static int sostrm_write(void *info, const char *buf, size_t len);
static int sostrm_expand(void *info, size_t extra);
static int sostrm_alloc(struct sostrm *ss, size_t req_buf);

static int snostrm_vprintf(void *info, const char *frmt, va_list ap);
static int snostrm_putc(void *info, int ch);
static int snostrm_write(void *info, const char *buf, size_t len);

static int fostrm_vprintf(void *info, const char *frmt, va_list ap);
static int fostrm_putc(void *info, int ch);
static int fostrm_write(void *info, const char *buf, size_t len);

static int costrm_vprintf(void *info, const char *frmt, va_list ap);
static int costrm_putc(void *info, int ch);
static int costrm_write(void *info, const char *buf, size_t len);
static int costrm_expand(void *info, size_t extra);

static int sistrm_getc(void *info);

static int fistrm_getc(void *info);

int
ostrm_printf(struct ostrm *strm, const char *frmt, ...)
{
	va_list ap;
	va_start(ap, frmt);
	int res = strm->vprintf(strm->info, frmt, ap);
	va_end(ap);
	return res;
}

int
ostrm_putc(struct ostrm *strm, int ch)
{
	return (strm->sputc != NULL)
	     ? strm->sputc(strm->info, ch)
	     : ostrm_printf(strm, "%c", ch);
}

int
ostrm_write(struct ostrm *strm, const char *buf, size_t len)
{
	if (strm->write != NULL)
		return strm->write(strm->info, buf, len);

	if (len == 0)
		return 0;

	for (size_t i = 0; i <= (len - 1); ++i) {
		if (ostrm_putc(strm, buf[i]) < 0)
			return -1;
	}

	return 0;
}

int
ostrm_expand(struct ostrm *strm, size_t extra)
{
	return (strm->expand != NULL)
	     ? strm->expand(strm->info, extra)
	     : 0;
}

void
ostrm_put_utf8(struct ostrm *ostr, unsigned cp)
{
	if (cp < 0x80) {
		ostrm_putc(ostr, cp);
	} else if (cp < 0x800) {
		ostrm_putc(ostr, 0xC0 |  (cp >> 6));
		ostrm_putc(ostr, 0x80 |  (cp & 0x3F));
	} else if (cp < 0x10000) {
		ostrm_putc(ostr, 0xE0 |  (cp >> 12));
		ostrm_putc(ostr, 0x80 | ((cp >>  6) & 0x3F));
		ostrm_putc(ostr, 0x80 |  (cp & 0x3F));
	} else {
		ostrm_putc(ostr, 0xF0 | ((cp >> 18) & 0x07));
		ostrm_putc(ostr, 0x80 | ((cp >> 12) & 0x3F));
		ostrm_putc(ostr, 0x80 | ((cp >>  6) & 0x3F));
		ostrm_putc(ostr, 0x80 |  (cp & 0x3F));
	}
}

void
ostrm_printi(struct ostrm *strm, chx_int num, char pad_char, int field_width, char misc_spec)
{
	if (field_width < 0)
		field_width = 0;

	bool upper = false;
	int base;

	switch (misc_spec) {
	case 'X': upper = true; /* fall through */
	case 'x': base = 16; break;
	case 'o': base = 8;  break;
	case 'b': base = 2;  break;
	default:  base = 10; break;
	}

	chx_int carry = 0, pos_num = num;
	if (pos_num < 0) {
		if (pos_num == CHX_INT_MIN) {
			pos_num = CHX_INT_MAX;
			carry = 1;
		} else {
			pos_num = -pos_num;
		}
	}

	char buf[1 + sizeof(chx_int) * CHAR_BIT * 2];
	int i = sizeof(buf) - 1;
	buf[i--] = '\0';

	for (; i >= 0; --i) {
		int digit = pos_num % base + carry;
		if (digit >= base) {
			carry = digit / base;
			digit = digit % base;
		} else {
			carry = 0;
		}

		if (digit < 10)
			buf[i] = digit + '0';
		else if (upper)
			buf[i] = (digit - 10) + 'A';
		else
			buf[i] = (digit - 10) + 'a';

		pos_num /= base;
		if (pos_num == 0)
			break;
	}

	int content_len = sizeof(buf) - 1 - i;
	if (num < 0) {
		++content_len;
		if (pad_char != ' ')
			ostrm_putc(strm, '-');
	}

	for (int j = 0; j < field_width - content_len; ++j)
		ostrm_putc(strm, pad_char);

	if (num < 0 && pad_char == ' ')
		ostrm_putc(strm, '-');

	ostrm_printf(strm, "%s", buf + i);
}


void
sostrm_init(struct sostrm *ss, CHEAX *c)
{
	ss->c = c;
	ss->buf = NULL;
	ss->idx = ss->cap = 0;

	ss->strm.info = ss;
	ss->strm.vprintf = sostrm_vprintf;
	ss->strm.sputc = sostrm_putc;
	ss->strm.write = sostrm_write;
	ss->strm.expand = sostrm_expand;
}

static int
sostrm_vprintf(void *info, const char *frmt, va_list ap)
{
	struct sostrm *strm = info;
	va_list len_ap;

	size_t rem = strm->cap - strm->idx;

	va_copy(len_ap, ap);
#if defined(HAVE_VSNPRINTF_L)
	int msg_len = vsnprintf_l(strm->buf + strm->idx, rem, get_c_locale(), frmt, len_ap);
#elif defined(HAVE_WINDOWS_VSNPRINTF_L)
	int msg_len = _vsnprintf_l(strm->buf + strm->idx, rem, frmt, get_c_locale(), len_ap);
#else
#define SHOULD_RESTORE_LOCALE
	locale_t prev_locale = uselocale(get_c_locale());
	int msg_len = vsnprintf(strm->buf + strm->idx, rem, frmt, len_ap);
#endif
	va_end(len_ap);

	if (msg_len < 0)
		goto msg_len_error;

	size_t req_buf = strm->idx + msg_len + 1; /* +1 for null byte */
	if (req_buf > strm->cap) {
		if (sostrm_alloc(strm, req_buf) < 0)
			return -1;

#if defined(HAVE_VSNPRINTF_L)
		msg_len = vsnprintf_l(strm->buf + strm->idx, msg_len + 1, get_c_locale(), frmt, ap);
#elif defined(HAVE_WINDOWS_VSNPRINTF_L)
		msg_len = _vsnprintf_l(strm->buf + strm->idx, msg_len + 1, frmt, get_c_locale(), ap);
#else
		msg_len = vsnprintf(strm->buf + strm->idx, msg_len + 1, frmt, ap);
#endif
		if (msg_len < 0)
			goto msg_len_error;
	}

#ifdef SHOULD_RESTORE_LOCALE
	uselocale(prev_locale);
#endif
	strm->idx += msg_len;
	return msg_len;

msg_len_error:
#ifdef SHOULD_RESTORE_LOCALE
#undef SHOULD_RESTORE_LOCALE
	uselocale(prev_locale);
#endif
	cheax_throwf(strm->c, CHEAX_EEVAL, "sostrm_printf(): internal error (vsnprintf returned %d)", msg_len);
	return -1;
}

static int
sostrm_putc(void *info, int ch)
{
	struct sostrm *strm = info;
	if (sostrm_alloc(strm, strm->idx + 1) < 0)
		return -1;

	strm->buf[strm->idx++] = ch;
	return ch;
}

static int
sostrm_write(void *info, const char *buf, size_t len)
{
	struct sostrm *strm = info;
	if (len > SIZE_MAX - strm->idx)
		return -1;

	if (sostrm_alloc(strm, strm->idx + len) < 0)
		return -1;

	memcpy(strm->buf + strm->idx, buf, len);
	strm->idx += len;
	return 0;
}

static int
sostrm_expand(void *info, size_t extra)
{
	struct sostrm *strm = info;
	if (extra > SIZE_MAX - strm->idx) {
		cheax_throwf(strm->c, CHEAX_ENOMEM, "sostrm_expand(): overflow");
		return -1;
	}

	return sostrm_alloc(strm, strm->idx + extra);
}

static int
sostrm_alloc(struct sostrm *strm, size_t req_buf)
{
	if (req_buf <= strm->cap)
		return 0;

	size_t alloc = req_buf;
	if (alloc <= SIZE_MAX - alloc / 2)
		alloc += alloc / 2;
	else
		alloc = SIZE_MAX;

	char *new_buf = cheax_realloc(strm->c, strm->buf, alloc);
	if (new_buf == NULL) {
		cheax_free(strm->c, strm->buf);
		strm->buf = NULL;
		strm->cap = strm->idx = 0;
		return -1;
	}

	strm->buf = new_buf;
	strm->cap = alloc;

	return 0;
}


void
snostrm_init(struct snostrm *ss, char *buf, size_t cap)
{
	ss->buf = memset(buf, 0, cap);
	ss->idx = 0;
	ss->cap = cap;

	ss->strm.info = ss;
	ss->strm.vprintf = snostrm_vprintf;
	ss->strm.sputc = snostrm_putc;
	ss->strm.write = snostrm_write;
	ss->strm.expand = NULL;
}

static int
snostrm_vprintf(void *info, const char *frmt, va_list ap)
{
	struct snostrm *strm = info;

	size_t rem = strm->cap - strm->idx;
#if defined(HAVE_VFPRINTF_L)
	int msg_len = vsnprintf_l(strm->buf + strm->idx, rem, get_c_locale(), frmt, ap);
#elif defined(HAVE_WINDOWS_VFPRINTF_L)
	int msg_len = _vsnprintf_l(strm->buf + strm->idx, rem, frmt, get_c_locale(), ap);
#else
	locale_t prev_locale = uselocale(get_c_locale());
	int msg_len = vsnprintf(strm->buf + strm->idx, rem, frmt, ap);
	uselocale(prev_locale);
#endif
	if (msg_len > 0)
		strm->idx = (strm->idx + msg_len > strm->cap) ? strm->cap : strm->idx + msg_len;
	return msg_len;
}

static int
snostrm_putc(void *info, int ch)
{
	struct snostrm *strm = info;
	if (strm->idx + 1 >= strm->cap)
		return EOF;

	strm->buf[strm->idx++] = ch;
	return ch;
}

static int
snostrm_write(void *info, const char *buf, size_t len)
{
	struct snostrm *strm = info;
	if (len > SIZE_MAX - strm->idx || strm->idx + len >= strm->cap)
		return -1;

	memcpy(strm->buf + strm->idx, buf, len);
	strm->idx += len;
	return 0;
}


void
fostrm_init(struct fostrm *fs, FILE *f, CHEAX *c)
{
	fs->c = c;
	fs->f = f;

	fs->strm.info = fs;
	fs->strm.vprintf = fostrm_vprintf;
	fs->strm.sputc = fostrm_putc;
	fs->strm.write = fostrm_write;
	fs->strm.expand = NULL;
}

static int
fostrm_vprintf(void *info, const char *frmt, va_list ap)
{
	struct fostrm *fs = info;
#if defined(HAVE_VFPRINTF_L)
	int res = vfprintf_l(fs->f, get_c_locale(), frmt, ap);
#elif defined(HAVE_WINDOWS_VFPRINTF_L)
	int res = _vfprintf_l(fs->f, frmt, get_c_locale(), ap);
#else
	locale_t prev_locale = uselocale(get_c_locale());
	int res = vfprintf(fs->f, frmt, ap);
	uselocale(prev_locale);
#endif
	if (res < 0)
		cheax_throwf(fs->c, CHEAX_EIO, "fostrm_vprintf(): vfprintf() returned negative value");
	return res;
}

static int
fostrm_putc(void *info, int ch)
{
	struct fostrm *fs = info;
	int res = fputc(ch, fs->f);
	if (res < 0)
		cheax_throwf(fs->c, CHEAX_EIO, "fostrm_putc(): fputc() returned negative value");
	return res;
}

static int
fostrm_write(void *info, const char *buf, size_t len)
{
	struct fostrm *fs = info;
	if (fwrite(buf, len, 1, fs->f) < 1) {
		cheax_throwf(fs->c, CHEAX_EIO, "fostrm_putc(): fwrite() unsuccessful");
		return -1;
	}

	return 0;
}


void
costrm_init(struct costrm *cs, struct ostrm *base)
{
	cs->base = base;
	cs->written = 0;

	cs->strm.info = cs;
	cs->strm.vprintf = costrm_vprintf;
	cs->strm.sputc = costrm_putc;
	cs->strm.write = costrm_write;
	cs->strm.expand = costrm_expand;
}

static int
costrm_vprintf(void *info, const char *frmt, va_list ap)
{
	struct costrm *cs = info;
	int written = cs->base->vprintf(cs->base->info, frmt, ap);
	if (written < 0)
		return -1;

	cs->written += written;
	return written;
}

static int
costrm_putc(void *info, int ch)
{
	struct costrm *cs = info;
	if (ostrm_putc(cs->base, ch) < 0)
		return -1;

	++cs->written;
	return 0;
}

static int
costrm_write(void *info, const char *buf, size_t len)
{
	struct costrm *cs = info;
	if (ostrm_write(cs->base, buf, len) < 0)
		return -1;

	cs->written += len;
	return 0;
}

static int
costrm_expand(void *info, size_t req)
{
	struct costrm *cs = info;
	return ostrm_expand(cs->base, req);
}


int
istrm_getc(struct istrm *strm)
{
	return strm->sgetc(strm->info);
}


void
sistrm_initn(struct sistrm *ss, const char *str, size_t len)
{
	ss->str = str;
	ss->len = len;
	ss->idx = 0;

	ss->strm.info = ss;
	ss->strm.sgetc = sistrm_getc;
}

void
sistrm_init(struct sistrm *ss, const char *str)
{
	sistrm_initn(ss, str, strlen(str));
}

static int
sistrm_getc(void *info)
{
	struct sistrm *ss = info;
	if (ss->idx >= ss->len)
		return EOF;

	return ss->str[ss->idx++];
}


void
fistrm_init(struct fistrm *fs, FILE *f, CHEAX *c)
{
	fs->c = c;
	fs->f = f;

	fs->strm.info = fs;
	fs->strm.sgetc = fistrm_getc;
}

static int
fistrm_getc(void *info)
{
	struct fistrm *ff = info;
	return fgetc(ff->f);
}


void
scnr_init(struct scnr *s, struct istrm *strm, size_t max_lah, int *lah_buf, int line, int pos)
{
	s->ch = 0;
	s->strm = strm;
	s->max_lah = max_lah;
	s->lah_buf = lah_buf;
	s->lah = 0;
	s->line = line;
	s->pos = pos;
	scnr_adv(s);
}

int
scnr_adv(struct scnr *s)
{
	int res = s->ch;
	if (res != EOF) {
		s->ch = (s->lah > 0) ? s->lah_buf[--s->lah] : istrm_getc(s->strm);
		if (s->ch == '\n') {
			s->pos = 0;
			++s->line;
		} else {
			++s->pos;
		}
	}
	return res;
}

int
scnr_backup(struct scnr *s, int to)
{
	if (s->lah >= s->max_lah)
		return -1;

	/* pray that there are no newlines involved */
	--s->pos;
	s->lah_buf[s->lah++] = s->ch;
	s->ch = to;
	return 0;
}
