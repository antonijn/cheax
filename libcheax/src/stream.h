#ifndef STREAM_H
#define STREAM_H

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "api.h"

/* output stream */
struct ostream {
	void *info;
	int (*vprintf)(void *info, const char *frmt, va_list ap);
	int (*putchar)(void *info, int ch);
};

static inline int
ostream_printf(struct ostream *stream, const char *frmt, ...)
{
	va_list ap;
	va_start(ap, frmt);
	int res = stream->vprintf(stream->info, frmt, ap);
	va_end(ap);
	return res;
}
static inline int
ostream_putchar(struct ostream *stream, int ch)
{
	return stream->putchar(stream->info, ch);
}

/* string output stream */
struct sostream {
	struct ostream ostr;

	CHEAX *c;
	char *buf;
	size_t idx, cap;
};

int sostream_expand(struct sostream *stream, size_t req_buf);
int sostream_vprintf(void *info, const char *frmt, va_list ap);
int sostream_putchar(void *info, int ch);

static inline void
sostream_init(struct sostream *ss, CHEAX *c)
{
	ss->c = c;
	ss->buf = NULL;
	ss->idx = ss->cap = 0;

	ss->ostr.info = ss;
	ss->ostr.vprintf = sostream_vprintf;
	ss->ostr.putchar = sostream_putchar;
}

/* file output stream */
struct fostream {
	struct ostream ostr;

	CHEAX *c;
	FILE *f;
};

int fostream_vprintf(void *info, const char *frmt, va_list ap);
int fostream_putchar(void *info, int ch);

static inline void
fostream_init(struct fostream *fs, FILE *f, CHEAX *c)
{
	fs->c = c;
	fs->f = f;

	fs->ostr.info = fs;
	fs->ostr.vprintf = fostream_vprintf;
	fs->ostr.putchar = fostream_putchar;
}

/* input stream */
struct istream {
	void *info;
	int (*getchar)(void *info);
};

static inline int
istream_getchar(struct istream *stream)
{
	return stream->getchar(stream->info);
}

/* string input stream */
struct sistream {
	struct istream istr;

	const char *str;
	size_t idx, len;
};

int sistream_getchar(void *info);

static inline void
sistream_init(struct sistream *ss, const char *str)
{
	ss->str = str;
	ss->len = strlen(str);
	ss->idx = 0;

	ss->istr.info = ss;
	ss->istr.getchar = sistream_getchar;
}

/* file input stream */
struct fistream {
	struct istream istr;

	CHEAX *c;
	FILE *f;
};

int fistream_getchar(void *info);

static inline void
fistream_init(struct fistream *fs, FILE *f, CHEAX *c)
{
	fs->c = c;
	fs->f = f;

	fs->istr.info = fs;
	fs->istr.getchar = fistream_getchar;
}

#endif
