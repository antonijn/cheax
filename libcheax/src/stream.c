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

#include "stream.h"

int
sostream_expand(struct sostream *stream, size_t req_buf)
{
	if (req_buf <= stream->cap)
		return 0;

	stream->cap = req_buf + req_buf / 2;
	size_t alloc = req_buf;
	if (alloc <= SIZE_MAX - alloc / 2)
		alloc += alloc / 2;
	else
		alloc = SIZE_MAX;

	char *new_buf = realloc(stream->buf, alloc);
	if (new_buf == NULL) {
		free(stream->buf);
		stream->buf = NULL;
		stream->cap = stream->idx = 0;

		cry(stream->c, "sostream_expand", CHEAX_ENOMEM, "out of memory");
		return -1;
	}

	stream->buf = new_buf;
	stream->cap = alloc;

	return 0;
}

int
sostream_vprintf(void *info, const char *frmt, va_list ap)
{
	struct sostream *stream = info;
	va_list len_ap;

	size_t rem = stream->cap - stream->idx;

	va_copy(len_ap, ap);
	int msg_len = vsnprintf(stream->buf + stream->idx, rem, frmt, len_ap);
	va_end(len_ap);

	if (msg_len < 0)
		goto msg_len_error;

	size_t req_buf = stream->idx + msg_len + 1; /* +1 for null byte */
	if (req_buf > stream->cap) {
		if (sostream_expand(stream, req_buf) < 0)
			return -1;

		msg_len = vsnprintf(stream->buf + stream->idx, msg_len + 1, frmt, ap);
		if (msg_len < 0)
			goto msg_len_error;
	}

	stream->idx += msg_len;

	return msg_len;

msg_len_error:
	cry(stream->c, "sostream_vprintf", CHEAX_EEVAL,
	    "internal error (vsnprintf returned %d)", msg_len);
	return -1;
}

int
sostream_putchar(void *info, int ch)
{
	struct sostream *stream = info;
	if (sostream_expand(stream, stream->idx + 1) < 0)
		return -1;

	stream->buf[stream->idx++] = ch;
	return ch;
}

int
fostream_vprintf(void *info, const char *frmt, va_list ap)
{
	struct fostream *fs = info;
	int res = vfprintf(fs->f, frmt, ap);
	if (res < 0)
		cry(fs->c, "fostream_vprintf", CHEAX_EIO, "vfprintf() returned negative value");
	return res;
}

int
fostream_putchar(void *info, int ch)
{
	struct fostream *fs = info;
	int res = fputc(ch, fs->f);
	if (res < 0)
		cry(fs->c, "fostream_putchar", CHEAX_EIO, "fputc() returned negative value");
	return res;
}

int
sistream_getchar(void *info)
{
	struct sistream *ss = info;
	if (ss->idx >= ss->len)
		return EOF;

	return ss->str[ss->idx++];
}

int
fistream_getchar(void *info)
{
	struct fistream *ff = info;
	return fgetc(ff->f);
}
