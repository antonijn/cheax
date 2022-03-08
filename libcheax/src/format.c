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
#include "format.h"
#include "print.h"
#include "strm.h"

static int
read_int(CHEAX *c, const char *desc, struct scnr *fmt, int *out)
{
	for (*out = 0; isdigit(fmt->ch); scnr_adv(fmt)) {
		if (*out > INT_MAX / 10 || (*out * 10) > INT_MAX - (fmt->ch - '0')) {
			cry(c, "format", CHEAX_EVALUE, "%s too big", desc);
			return -1;
		}
		*out = (*out * 10) + (fmt->ch - '0');
	}

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
read_fspec(CHEAX *c, struct scnr *fmt, struct fspec *sp)
{
	sp->index = -1;
	sp->conv = CONV_NONE;
	sp->pad_char = ' ';
	sp->field_width = 0;
	sp->precision = -1;
	sp->misc_spec = '\0';

	if (isdigit(fmt->ch) && read_int(c, "index", fmt, &sp->index) < 0)
		return -1;

	if (fmt->ch == '!') {
		scnr_adv(fmt);
		if (fmt->ch == 's' || fmt->ch == 'r') {
			sp->conv = (scnr_adv(fmt) == 's') ? CONV_S : CONV_R;
		} else {
			cry(c, "format", CHEAX_EVALUE, "expected `s' or `r' after `!'");
			return -1;
		}
	}

	if (fmt->ch == ':') {
		scnr_adv(fmt);
		if (fmt->ch == ' ' || fmt->ch == '0')
			sp->pad_char = scnr_adv(fmt);

		if (isdigit(fmt->ch) && read_int(c, "field width", fmt, &sp->field_width) < 0)
			return -1;

		if (fmt->ch == '.') {
			scnr_adv(fmt);
			if (!isdigit(fmt->ch)) {
				cry(c, "format", CHEAX_EVALUE, "expected precision specifier");
				return -1;
			}

			if (read_int(c, "precision", fmt, &sp->precision) < 0)
				return -1;
		}

		if (fmt->ch != '\0' && strchr("xXobcdeEfFgG", fmt->ch) != NULL)
			sp->misc_spec = scnr_adv(fmt);
	}

	if (scnr_adv(fmt) != '}') {
		cry(c, "format", CHEAX_EVALUE, "expected `}'");
		return -1;
	}

	return 0;
}

static int
format_fspec(CHEAX *c, struct sostrm *ss, struct fspec *sp, struct chx_value *arg)
{
	struct ostrm *strm = &ss->strm;
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
	if (ss->idx > SIZE_MAX - ufield_width || sostrm_expand(ss, ss->idx + ufield_width) < 0)
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

			ostrm_putc(strm, num);
		} else {
			ostrm_printi(strm, num, sp->pad_char, sp->field_width, sp->misc_spec);
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

		ostrm_printf(strm, fmt_buf, sp->field_width, sp->precision, num);
	} else {
		if (!can_other) {
			cry(c, "format", CHEAX_EVALUE, "invalid specifiers for given value");
			return -1;
		}

		if (ty == CHEAX_STRING && sp->conv != CONV_R) {
			/* can't do ostrm_printf() in case string
			 * contains null character */
			struct chx_string *str = (struct chx_string *)arg;
			for (size_t i = 0; i < str->len; ++i)
				ostrm_putc(strm, str->value[i]);
		} else {
			ostrm_show(c, strm, arg);
		}
	}

	/* Add padding if necessary.
	 * TODO this should probably count graphemes rather than bytes */
	int written = ss->idx - prev_idx;
	int padding = sp->field_width - written;

	for (int i = 0; i < padding; ++i)
		ostrm_putc(strm, sp->pad_char);

	return 0;
}

static struct chx_value *
scnr_format(CHEAX *c, struct scnr *fmt, struct chx_list *args, size_t size_hint)
{
	struct chx_value **arg_array;
	size_t arg_count;
	if (0 != cheax_list_to_array(c, args, &arg_array, &arg_count))
		return NULL;

	struct sostrm ss;
	sostrm_init(&ss, c);

	ss.cap = size_hint;
	ss.buf = malloc(ss.cap);

	enum { UNSPECIFIED, AUTO_IDX, MAN_IDX } indexing = UNSPECIFIED;
	size_t auto_idx = 0;
	struct chx_value *res = NULL;

	for (;;) {
		if (fmt->ch == EOF) {
			res = &cheax_nstring(c, ss.buf, ss.idx)->base;
			break;
		}

		if (fmt->ch == '}' && (scnr_adv(fmt), fmt->ch) != '}') {
			cry(c, "format", CHEAX_EVALUE, "encountered single `}' in format string");
			break;
		}

		if (fmt->ch != '{' || (scnr_adv(fmt), fmt->ch) == '{') {
			/* most likely condition: 'regular' character or
			 * double "{{" */
			sostrm_putc(&ss, scnr_adv(fmt));
			continue;
		}

		/* we're dealing with a field specifier */

		struct fspec spec;
		if (read_fspec(c, fmt, &spec) < 0)
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

struct chx_value *
format(CHEAX *c, struct chx_string *fmt, struct chx_list *args)
{
	struct sistrm ss;
	sistrm_initn(&ss, fmt->value, fmt->len);

	struct scnr s;
	scnr_init(&s, &ss.strm, 0, NULL);
	return scnr_format(c, &s, args, ss.len + 1);
}

struct chx_value *
cheax_format(CHEAX *c, const char *fmt, struct chx_list *args)
{
	if (fmt == NULL) {
		cry(c, "format", CHEAX_EAPI, "`fmt' cannot be NULL");
		return NULL;
	}

	struct sistrm ss;
	sistrm_init(&ss, fmt);

	struct scnr s;
	scnr_init(&s, &ss.strm, 0, NULL);

	return scnr_format(c, &s, args, ss.len + 1);
}
