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
#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>

#include "api.h"
#include "print.h"
#include "stream.h"

/* Print integer `num' to ostream `ostr', padding it to length
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
static void
ostream_print_int(struct ostream *ostr, int num, char pad_char, int field_width, char misc_spec)
{
	if (field_width < 0)
		field_width = 0;

	bool upper = false;
	int base;

	switch (misc_spec) {
	case 'X': upper = true;
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
			ostream_printf(ostr, "-");
	}

	for (int j = 0; j < field_width - content_len; ++j)
		ostream_putchar(ostr, pad_char);

	if (num < 0 && pad_char == ' ')
		ostream_printf(ostr, "-");

	ostream_printf(ostr, "%s", buf + i);
}

static int
read_int(CHEAX *c, const char *desc, const char **fmt_in, int *out)
{
	const char *fmt = *fmt_in;

	for (*out = 0; isdigit(*fmt); ++fmt) {
		if (*out > INT_MAX / 10 || (*out * 10) > INT_MAX - (*fmt - '0')) {
			cry(c, "format", CHEAX_EVALUE, "%s too big", desc);
			return -1;
		}
		*out = (*out * 10) + (*fmt - '0');
	}

	*fmt_in = fmt;
	return 0;
}

/* format (or field) specifier */
struct fspec {
	int index;       /* field index (default -1) */

	/* conversion specifier */
	enum {
		CONV_NONE,
		CONV_S,  /* for {!s} */
		CONV_R,  /* for {!r} */
	} conv;

	char pad_char;   /* character to pad with: ' ' (default) or '0' */
	int field_width; /* width (number of bytes) to pad to (default 0) */
	int precision;   /* floating point precision spec (default -1) */
	char misc_spec;  /* xXobcd for int, eEfFgG for double (default 0) */
};

static int
read_fspec(CHEAX *c, const char **fmt_in, struct fspec *sp)
{
	sp->index = -1;
	sp->conv = CONV_NONE;
	sp->pad_char = ' ';
	sp->field_width = 0;
	sp->precision = -1;
	sp->misc_spec = '\0';

	const char *fmt = *fmt_in;

	if (isdigit(*fmt) && read_int(c, "index", &fmt, &sp->index) < 0)
		return -1;

	if (*fmt == '!') {
		if (*++fmt == 's' || *fmt == 'r') {
			sp->conv = (*fmt++ == 's') ? CONV_S : CONV_R;
		} else {
			cry(c, "format", CHEAX_EVALUE, "expected `s' or `r' after `!'");
			return -1;
		}
	}

	if (*fmt == ':') {
		if (*++fmt == ' ' || *fmt == '0')
			sp->pad_char = *fmt++;

		if (isdigit(*fmt) && read_int(c, "field width", &fmt, &sp->field_width) < 0)
			return -1;

		if (*fmt == '.') {
			if (!isdigit(*++fmt)) {
				cry(c, "format", CHEAX_EVALUE, "expected precision specifier");
				return -1;
			}

			if (read_int(c, "precision", &fmt, &sp->precision) < 0)
				return -1;
		}

		if (*fmt != '\0' && strchr("xXobcdeEfFgG", *fmt) != NULL)
			sp->misc_spec = *fmt++;
	}

	if (*fmt++ != '}') {
		cry(c, "format", CHEAX_EVALUE, "expected `}'");
		return -1;
	}

	*fmt_in = fmt;
	return 0;
}

static int
format_fspec(CHEAX *c, struct sostream *ss, struct fspec *sp, struct chx_value *arg)
{
	bool can_int = true, can_double = true, can_other = true;

	if (sp->precision != -1)
		can_int = can_other = false;

	if (sp->misc_spec != '\0') {
		if (strchr("xXobcd", sp->misc_spec) != NULL)
			can_double = can_other = false;
		else if (strchr("eEfFgG", sp->misc_spec) != NULL)
			can_int = can_other = false;
	}

	/* this is primarily so we can catch any ENOMEM early if
	 * field_width is too big, instead of letting the mem use slowly
	 * creep up as we add more and more padding. */
	size_t ufield_width = sp->field_width;
	if (ss->idx > SIZE_MAX - ufield_width || sostream_expand(ss, ss->idx + ufield_width) < 0)
		return -1;

	/* Used to gauge if padding is necessary. With numbers, the
	 * padding should be correct as it is, but there can still be
	 * edge-cases (like the `:c' specifier to integers). */
	int prev_idx = ss->idx;

	int ty = cheax_type_of(arg);

	if (sp->conv == CONV_NONE && ty == CHEAX_INT) {
		if (!can_int) {
			cry(c, "format", CHEAX_EVALUE, "invalid specifiers for integer");
			return -1;
		}

		int num = ((struct chx_int *)arg)->value;

		if (sp->misc_spec == 'c') {
			if (num < 0 || num >= 256) {
				cry(c, "format", CHEAX_EVALUE, "invalid character %d", num);
				return -1;
			}

			ostream_putchar(&ss->ostr, num);
		} else {
			ostream_print_int(&ss->ostr, num, sp->pad_char, sp->field_width, sp->misc_spec);
		}
	} else if (sp->conv == CONV_NONE && ty == CHEAX_DOUBLE) {
		if (!can_double) {
			cry(c, "format", CHEAX_EVALUE, "invalid specifiers for double");
			return -1;
		}

		double num = ((struct chx_double *)arg)->value;
		char ms = (sp->misc_spec != '\0') ? sp->misc_spec : 'g';

		char fmt_buf[32];
		if (sp->pad_char == ' ')
			snprintf(fmt_buf, sizeof(fmt_buf), "%%*.*%c", ms);
		else
			snprintf(fmt_buf, sizeof(fmt_buf), "%%%c*.*%c", sp->pad_char, ms);

		ostream_printf(&ss->ostr, fmt_buf, sp->field_width, sp->precision, num);
	} else {
		if (!can_other) {
			cry(c, "format", CHEAX_EVALUE, "invalid specifiers for given value");
			return -1;
		}

		if (ty == CHEAX_STRING && sp->conv != CONV_R) {
			/* can't do ostream_printf() in case string
			 * contains null character */
			struct chx_string *str = (struct chx_string *)arg;
			for (int i = 0; i < str->len; ++i)
				ostream_putchar(&ss->ostr, str->value[i]);
		} else {
			ostream_show(c, &ss->ostr, arg);
		}
	}

	/* Add padding if necessary.
	 * TODO this should probably count graphemes rather than bytes */
	int written = ss->idx - prev_idx;
	int padding = sp->field_width - written;

	for (int i = 0; i < padding; ++i)
		ostream_putchar(&ss->ostr, sp->pad_char);

	return 0;
}

struct chx_value *
cheax_format(CHEAX *c, const char *fmt, struct chx_list *args)
{
	if (fmt == NULL) {
		cry(c, "format", CHEAX_EAPI, "`fmt' cannot be NULL");
		return NULL;
	}

	struct chx_value **arg_array;
	size_t arg_count;
	if (0 != cheax_list_to_array(c, args, &arg_array, &arg_count))
		return NULL;

	struct sostream ss;
	sostream_init(&ss, c);

	ss.cap = strlen(fmt) + 1; /* conservative size estimate */
	ss.buf = malloc(ss.cap);

	enum { UNSPECIFIED, AUTO_IDX, MAN_IDX } indexing = UNSPECIFIED;
	size_t auto_idx = 0;
	struct chx_value *res = NULL;

	for (;;) {
		if (*fmt == '\0') {
			res = &cheax_nstring(c, ss.buf, ss.idx)->base;
			break;
		}

		if (*fmt == '}' && *++fmt != '}') {
			cry(c, "format", CHEAX_EVALUE, "encountered single `}' in format string");
			break;
		}

		if (*fmt != '{' || *++fmt == '{') {
			/* most likely condition: 'regular' character or
			 * double "{{" */
			ostream_putchar(&ss.ostr, *fmt++);
			continue;
		}

		/* we're dealing with a field specifier */

		struct fspec spec;
		if (read_fspec(c, &fmt, &spec) < 0)
			break;

		if (indexing == AUTO_IDX && spec.index != -1) {
			cry(c, "format", CHEAX_EVALUE, "cannot switch from automatic indexing to manual indexing");
			break;
		}

		if (indexing == MAN_IDX && spec.index == -1) {
			cry(c, "format", CHEAX_EVALUE, "expected index (cannot switch from manual indexing to automatic indexing)");
			break;
		}

		if (indexing == UNSPECIFIED)
			indexing = (spec.index == -1) ? AUTO_IDX : MAN_IDX;

		size_t idx = (indexing == AUTO_IDX) ? auto_idx++ : (size_t)spec.index;
		if (idx >= arg_count) {
			cry(c, "format", CHEAX_EINDEX, "too few arguments");
			break;
		}

		if (format_fspec(c, &ss, &spec, arg_array[idx]) < 0)
			break;
	}

	free(arg_array);
	free(ss.buf);
	return res;
}
