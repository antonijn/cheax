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

#include "strm.h"

void
ostrm_printi(struct ostrm *strm, int num, char pad_char, int field_width, char misc_spec)
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

	long pos_num = num;
	if (pos_num < 0)
		pos_num = -pos_num;

	char buf[1 + sizeof(int) * 8 * 2];
	int i = sizeof(buf) - 1;
	buf[i--] = '\0';

	for (; i >= 0; --i) {
		int digit = pos_num % base;
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

int
sostrm_expand(struct sostrm *strm, size_t req_buf)
{
	if (req_buf <= strm->cap)
		return 0;

	strm->cap = req_buf + req_buf / 2;
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

int
sostrm_vprintf(void *info, const char *frmt, va_list ap)
{
	struct sostrm *strm = info;
	va_list len_ap;

	size_t rem = strm->cap - strm->idx;

	va_copy(len_ap, ap);
	int msg_len = vsnprintf(strm->buf + strm->idx, rem, frmt, len_ap);
	va_end(len_ap);

	if (msg_len < 0)
		goto msg_len_error;

	size_t req_buf = strm->idx + msg_len + 1; /* +1 for null byte */
	if (req_buf > strm->cap) {
		if (sostrm_expand(strm, req_buf) < 0)
			return -1;

		msg_len = vsnprintf(strm->buf + strm->idx, msg_len + 1, frmt, ap);
		if (msg_len < 0)
			goto msg_len_error;
	}

	strm->idx += msg_len;

	return msg_len;

msg_len_error:
	cry(strm->c, "sostrm_vprintf", CHEAX_EEVAL,
	    "internal error (vsnprintf returned %d)", msg_len);
	return -1;
}

int
sostrm_putc(void *info, int ch)
{
	struct sostrm *strm = info;
	if (sostrm_expand(strm, strm->idx + 1) < 0)
		return -1;

	strm->buf[strm->idx++] = ch;
	return ch;
}

int
fostrm_vprintf(void *info, const char *frmt, va_list ap)
{
	struct fostrm *fs = info;
	int res = vfprintf(fs->f, frmt, ap);
	if (res < 0)
		cry(fs->c, "fostrm_vprintf", CHEAX_EIO, "vfprintf() returned negative value");
	return res;
}

int
fostrm_putc(void *info, int ch)
{
	struct fostrm *fs = info;
	int res = fputc(ch, fs->f);
	if (res < 0)
		cry(fs->c, "fostrm_putc", CHEAX_EIO, "fputc() returned negative value");
	return res;
}

int
sistrm_getc(void *info)
{
	struct sistrm *ss = info;
	if (ss->idx >= ss->len)
		return EOF;

	return ss->str[ss->idx++];
}

int
fistrm_getc(void *info)
{
	struct fistrm *ff = info;
	return fgetc(ff->f);
}

int
scnr_adv(struct scnr *s)
{
	int res = s->ch;
	if (res != EOF) {
		int pop;
		/* offset by one to avoid unsigned shenanigans */
		for (size_t i = s->lah; i >= 1; --i) {
			int next_pop = s->lah_buf[i - 1];
			s->lah_buf[i - 1] = pop;
			pop = next_pop;
		}

		if (s->lah == 0) {
			s->ch = istrm_getc(s->strm);
		} else {
			--s->lah;
			s->ch = pop;
		}

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

	++s->lah;

	int push = s->ch;
	for (size_t i = 0; i < s->lah; ++i) {
		int next_push = s->lah_buf[i];
		s->lah_buf[i] = push;
		push = next_push;
	}
	s->ch = to;
	return 0;
}
