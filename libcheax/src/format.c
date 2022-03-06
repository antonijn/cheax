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
	/* conversion specifier */
	enum {
		CONV_NONE,
		CONV_S,  /* for {!s} */
		CONV_R,  /* for {!r} */
	} conv;

	char pad_char;   /* character to pad with: ' ' (default) or '0' */
	char misc_spec;  /* xXobcd for int, eEfFgG for double (default 0) */
	int field_width; /* width (number of bytes) to pad to (default 0) */
	int precision;   /* floating point precision spec (default -1) */

	/* whether specifier can apply to int field, double field or any
	 * other type of field, respectively */
	bool can_int, can_double, can_other;
};

/* indexing mode */
enum {
	UNSPECIFIED,
	AUTO_IDX,    /* for {} indices */
	MAN_IDX,     /* for {idx} indices */
};

static int
read_fspec(CHEAX *c, const char **fmt_in, struct fspec *sp, int *indexing, int *cur_arg_idx)
{
	sp->conv = CONV_NONE;
	sp->pad_char = ' ';
	sp->misc_spec = '\0';
	sp->field_width = 0;
	sp->precision = -1;
	sp->can_int = sp->can_double = sp->can_other = true;

	const char *fmt = *fmt_in;

	if (isdigit(*fmt)) {
		if (*indexing == AUTO_IDX) {
			cry(c, "format", CHEAX_EVALUE,
			    "cannot switch from automatic indexing to manual indexing");
			return -1;
		}

		*indexing = MAN_IDX;

		if (read_int(c, "index", &fmt, cur_arg_idx) < 0)
			return -1;
	} else if (*indexing == MAN_IDX) {
		cry(c, "format", CHEAX_EVALUE,
		    "expected index (cannot switch from manual indexing to automatic indexing)");
		return -1;
	} else {
		*indexing = AUTO_IDX;
	}

	if (*fmt == '!') {
		++fmt;
		if (*fmt == 's' || *fmt == 'r') {
			sp->conv = (*fmt++ == 's') ? CONV_S : CONV_R;
		} else {
			cry(c, "format", CHEAX_EVALUE, "expected `s' or `r' after `!'");
			return -1;
		}
	}

	if (*fmt == ':') {
		++fmt;
		if (*fmt == ' ' || *fmt == '0')
			sp->pad_char = *fmt++;

		if (isdigit(*fmt) && read_int(c, "field width", &fmt, &sp->field_width) < 0)
			return -1;

		if (*fmt == '.') {
			sp->can_int = sp->can_other = false;
			if (!isdigit(*++fmt)) {
				cry(c, "format", CHEAX_EVALUE, "expected precision specifier");
				return -1;
			}

			if (read_int(c, "precision", &fmt, &sp->precision) < 0)
				return -1;
		}

		if (strchr("xXobcd", *fmt) != NULL) {
			sp->can_double = sp->can_other = false;
			sp->misc_spec = *fmt++;
		} else if (strchr("eEfFgG", *fmt) != NULL) {
			sp->can_int = sp->can_other = false;
			sp->misc_spec = *fmt++;
		}
	}

	if (*fmt++ != '}') {
		cry(c, "format", CHEAX_EVALUE, "expected `}'");
		return -1;
	}

	*fmt_in = fmt;
	return 0;
}

static int
format_fspec(CHEAX *c,
             struct sostream *ss,
             struct fspec *sp,
             int cur_arg_idx,
             struct chx_value **arg_array,
             size_t arg_count)
{
	if (cur_arg_idx >= arg_count) {
		cry(c, "format", CHEAX_EINDEX, "too few arguments");
		return -1;
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

	struct chx_value *arg = arg_array[cur_arg_idx];
	int ty = cheax_type_of(arg);

	if (sp->conv == CONV_NONE && ty == CHEAX_INT) {
		if (!sp->can_int) {
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
		if (!sp->can_double) {
			cry(c, "format", CHEAX_EVALUE, "invalid specifiers for double");
			return -1;
		}

		double num = ((struct chx_double *)arg)->value;

		if (sp->misc_spec == '\0')
			sp->misc_spec = 'g';

		char fmt_buf[32];
		if (sp->pad_char == ' ')
			snprintf(fmt_buf, sizeof(fmt_buf), "%%*.*%c", sp->misc_spec);
		else
			snprintf(fmt_buf, sizeof(fmt_buf), "%%%c*.*%c", sp->pad_char, sp->misc_spec);

		ostream_printf(&ss->ostr, fmt_buf, sp->field_width, sp->precision, num);
	} else {
		if (!sp->can_other) {
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

	struct sostream ss;
	sostream_init(&ss, c);

	ss.cap = strlen(fmt) + 1; /* conservative size estimate */
	ss.buf = malloc(ss.cap);

	int indexing = UNSPECIFIED;
	int cur_arg_idx = 0;
	struct chx_value *res = NULL;

	struct chx_value **arg_array;
	size_t arg_count;
	if (0 != cheax_list_to_array(c, args, &arg_array, &arg_count))
		goto done;

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
			ostream_putchar(&ss.ostr, *fmt++);
			continue;
		}

		struct fspec spec;
		if (read_fspec(c, &fmt, &spec, &indexing, &cur_arg_idx) < 0
		 || format_fspec(c, &ss, &spec, cur_arg_idx, arg_array, arg_count) < 0)
		{
			break;
		}

		if (indexing == AUTO_IDX)
			++cur_arg_idx;
	}

done:
	free(arg_array);
	free(ss.buf);
	return res;
}
