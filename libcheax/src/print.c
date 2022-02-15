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
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "api.h"
#include "stream.h"

static void ostream_show(CHEAX *c, struct ostream *s, struct chx_value *val);

static void
ostream_show_basic_type(CHEAX *c, struct ostream *s, struct chx_value *val)
{
	switch (cheax_resolve_type(c, cheax_type_of(val))) {
	case CHEAX_NIL:
		ostream_printf(s, "()");
		break;
	case CHEAX_INT:
		ostream_printf(s, "%d", ((struct chx_int *)val)->value);
		break;
	case CHEAX_DOUBLE:
		ostream_printf(s, "%f", ((struct chx_double *)val)->value);
		break;
	case CHEAX_ID:
		ostream_printf(s, "%s", ((struct chx_id *)val)->id);
		break;
	case CHEAX_LIST:
		ostream_putchar(s, '(');
		for (struct chx_list *list = (struct chx_list *)val; list; list = list->next) {
			ostream_show(c, s, list->value);
			if (list->next)
				ostream_putchar(s, ' ');
		}
		ostream_putchar(s, ')');
		break;
	case CHEAX_QUOTE:
		ostream_putchar(s, '\'');
		ostream_show(c, s, ((struct chx_quote *)val)->value);
		break;
	case CHEAX_BACKQUOTE:
		ostream_putchar(s, '`');
		ostream_show(c, s, ((struct chx_quote *)val)->value);
		break;
	case CHEAX_COMMA:
		ostream_putchar(s, ',');
		ostream_show(c, s, ((struct chx_quote *)val)->value);
		break;
	case CHEAX_FUNC:
		ostream_putchar(s, '(');
		struct chx_func *func = (struct chx_func *)val;
		if (func->eval_args)
			ostream_printf(s, "\\ ");
		else
			ostream_printf(s, "\\\\ ");
		ostream_show(c, s, func->args);
		struct chx_list *body = func->body;
		for (; body; body = body->next) {
			ostream_printf(s, "\n  ");
			ostream_show(c, s, body->value);
		}
		ostream_putchar(s, ')');
		break;
	case CHEAX_STRING:
		ostream_putchar(s, '"');
		struct chx_string *string = (struct chx_string *)val;
		for (int i = 0; i < string->len; ++i) {
			char ch = string->value[i];
			if (ch == '"')
				ostream_printf(s, "\\\"");
			else if (isprint(ch))
				ostream_putchar(s, ch);
			else
				ostream_printf(s, "\\x%02x", (unsigned)ch & 0xFFu);
		}
		ostream_putchar(s, '"');
		break;
	case CHEAX_EXT_FUNC:
		; struct chx_ext_func *macro = (struct chx_ext_func *)val;
		if (macro->name == NULL)
			ostream_printf(s, "[built-in function]");
		else
			ostream_printf(s, "%s", macro->name);
		break;
	case CHEAX_USER_PTR:
		ostream_printf(s, "%p", ((struct chx_user_ptr *)val)->value);
		break;
	}
}

static void
ostream_show(CHEAX *c, struct ostream *s, struct chx_value *val)
{
	int ty = cheax_type_of(val);
	if (cheax_is_basic_type(c, ty)) {
		ostream_show_basic_type(c, s, val);
	} else {
		ostream_printf(s, "(%s ", c->typestore.array[ty - CHEAX_TYPESTORE_BIAS].name);
		ostream_show_basic_type(c, s, val);
		ostream_printf(s, ")");
	}
}

void
cheax_print(CHEAX *c, FILE *f, struct chx_value *val)
{
	struct fostream fs;
	fostream_init(&fs, f, c);
	ostream_show(c, &fs.ostr, val);
}

/* Print integer `num' to ostream `ostr', padding it to length `field_width' using padding character `pad_char' if necessary. `misc_spec' can be: 'X':   0xdeadbeef => "DEADBEEF" (uppercase hex) 'x':   0xdeadbeef => "deadbeef" (lowercase hex) 'o':   71         => "107"      (octal)
 * 'b':   123        => "1111011"  (binary)
 * other: 123        => "123"      (decimal)
 *
 * Why not just a printf() variant, I hear you ask. The problem with
 * printf() is that, depending on format specifier, it expects different
 * data types (unsigned int for 'x', for instance). This one just
 * handles int, and handles it well.
 */
void
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

/*
 * Fun fact: this function contains over 40 goto statements! This is
 * because it's just a hand-written state-machine-based parser. I hope
 * you'll forgive me.
 */
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

	enum {
		UNSPECIFIED,
		AUTO_IDX,    /* for {} indices */
		MAN_IDX,     /* for {idx} indices */
	} indexing = UNSPECIFIED;

	enum {
		CONV_NONE,
		CONV_S,   /* for {!s} */
		CONV_R,   /* for {!r} */
	} conv;                              /* set per specifier */

	char pad_char, misc_spec;            /* ditto */
	int field_width, precision;          /* --"-- */
	bool can_int, can_double, can_other; /* --"-- */

	int cur_arg_idx = 0;
	struct chx_value *res = NULL;

	struct chx_value **arg_array;
	size_t arg_count;
	if (0 != cheax_list_to_array(c, args, &arg_array, &arg_count))
		goto error;

	char ch;

normal:
	ch = *fmt;
	if (ch == '\0')
		goto finish;

	++fmt;
	if (ch == '{')
		goto entered_curly;
	if (ch == '}')
		goto single_end_curly;

	ostream_putchar(&ss.ostr, ch);
	goto normal;

entered_curly:
	ch = *fmt;

	/* set the per-specifier settings here */
	conv = CONV_NONE;
	pad_char = ' ';
	field_width = 0;
	precision = -1;
	misc_spec = '\0';
	can_int = can_double = can_other = true;

	if (ch == '{') {
		ostream_putchar(&ss.ostr, ch);
		++fmt;
		goto normal;
	}

	if (isdigit(ch)) {
		if (indexing == AUTO_IDX) {
			cry(c, "format", CHEAX_EVALUE,
			    "cannot switch from automatic indexing to manual indexing");
			goto error;
		}

		indexing = MAN_IDX;
		cur_arg_idx = 0;
		goto read_idx;
	} else if (indexing == MAN_IDX) {
		cry(c, "format", CHEAX_EVALUE,
		    "expected index (cannot switch from manual indexing to automatic indexing)");
		goto error;
	} else {
		indexing = AUTO_IDX;
	}

	goto read_any_conv;

read_idx:
	cur_arg_idx = ch - '0';
	++fmt;
	goto read_more_idx;

read_more_idx:
	ch = *fmt;
	if (!isdigit(ch))
		goto read_any_conv;

	if (cur_arg_idx > INT_MAX / 10 || (cur_arg_idx * 10) > INT_MAX - (ch - '0')) {
		cry(c, "format", CHEAX_EVALUE, "index too big");
		goto error;
	}

	cur_arg_idx = (cur_arg_idx * 10) + (ch - '0');
	++fmt;
	goto read_more_idx;

read_any_conv:
	ch = *fmt;
	if (ch == '!') {
		++fmt;
		goto read_conv;
	}

	goto read_any_format_specs;

read_conv:
	ch = *fmt;
	if (ch == 's') {
		conv = CONV_S;
		++fmt;
		goto read_any_format_specs;
	}
	if (ch == 'r') {
		conv = CONV_R;
		++fmt;
		goto read_any_format_specs;
	}

	cry(c, "format", CHEAX_EVALUE, "expected `s' or `r' after `!'");
	goto error;

read_any_format_specs:
	ch = *fmt;

	if (ch == ':') {
		++fmt;
		goto read_format_specs;
	}

	goto read_closing_curly;

read_format_specs:
	ch = *fmt;
	if (ch == ' ' || ch == '0') {
		pad_char = ch;
		++fmt;
		goto read_field_width;
	}

	if (isdigit(ch))
		goto read_field_width;

	goto read_any_precision_spec;

read_field_width:
	ch = *fmt;
	if (isdigit(ch)) {
		if (field_width > INT_MAX / 10 || (field_width * 10) > INT_MAX - (ch - '0')) {
			cry(c, "format", CHEAX_EVALUE, "field width too big");
			goto error;
		}

		field_width = (field_width * 10) + (ch - '0');
		++fmt;
		goto read_field_width;
	}

	goto read_any_precision_spec;

read_any_precision_spec:
	ch = *fmt;
	if (ch == '.') {
		can_int = can_other = false;
		++fmt;
		goto read_precision_spec;
	}

	goto read_misc_spec;

read_precision_spec:
	ch = *fmt;
	if (!isdigit(ch)) {
		cry(c, "format", CHEAX_EVALUE, "expected precision specifier");
		goto error;
	}

	precision = ch - '0';
	++fmt;
	goto read_more_precision_spec;

read_more_precision_spec:
	ch = *fmt;
	if (!isdigit(ch))
		goto read_misc_spec;

	if (precision > INT_MAX / 10 || (precision * 10) > INT_MAX - (ch - '0')) {
		cry(c, "format", CHEAX_EVALUE, "precision too big");
		goto error;
	}

	precision = (precision * 10) + (ch - '0');
	++fmt;
	goto read_more_precision_spec;

read_misc_spec:
	ch = *fmt;
	if (strchr("xXobcd", ch) != NULL) {
		can_double = can_other = false;
		misc_spec = ch;
		++fmt;
	} else if (strchr("eEfFgG", ch) != NULL) {
		can_int = can_other = false;
		misc_spec = ch;
		++fmt;
	}

	goto read_closing_curly;

read_closing_curly:
	ch = *fmt;
	if (ch != '}') {
		cry(c, "format", CHEAX_EVALUE, "expected `}'");
		goto error;
	}

	if (cur_arg_idx >= arg_count) {
		cry(c, "format", CHEAX_EINDEX, "too few arguments");
		goto error;
	}

	/* this is primarily so we can catch any ENOMEM early if
	 * field_width is too big, instead of letting the mem use slowly
	 * creep up as we add more and more padding. */
	size_t ufield_width = field_width;
	if (ss.idx > SIZE_MAX - ufield_width || sostream_expand(&ss, ss.idx + ufield_width) < 0)
		goto error;

	/* Used to gauge if padding is necessary. With numbers, the
	 * padding should be correct as it is, but there can still be
	 * edge-cases (like the `:c' specifier to integers). */
	int prev_idx = ss.idx;

	struct chx_value *arg = arg_array[cur_arg_idx];
	int ty = cheax_type_of(arg);

	if (conv == CONV_NONE && ty == CHEAX_INT) {
		if (!can_int) {
			cry(c, "format", CHEAX_EVALUE, "invalid specifiers for integer");
			goto error;
		}

		int num = ((struct chx_int *)arg)->value;

		if (misc_spec == 'c') {
			if (num < 0 || num >= 256) {
				cry(c, "format", CHEAX_EVALUE, "invalid character %d", num);
				goto error;
			}

			ostream_putchar(&ss.ostr, num);
		} else {
			ostream_print_int(&ss.ostr, num, pad_char, field_width, misc_spec);
		}
	} else if (conv == CONV_NONE && ty == CHEAX_DOUBLE) {
		if (!can_double) {
			cry(c, "format", CHEAX_EVALUE, "invalid specifiers for double");
			goto error;
		}

		double num = ((struct chx_double *)arg)->value;

		if (misc_spec == '\0')
			misc_spec = 'g';

		char fmt_buf[32];
		if (pad_char == ' ')
			snprintf(fmt_buf, sizeof(fmt_buf), "%%*.*%c", misc_spec);
		else
			snprintf(fmt_buf, sizeof(fmt_buf), "%%%c*.*%c", pad_char, misc_spec);

		ostream_printf(&ss.ostr, fmt_buf, field_width, precision, num);
	} else {
		if (!can_other) {
			cry(c, "format", CHEAX_EVALUE, "invalid specifiers for given value");
			goto error;
		}

		if (ty == CHEAX_STRING && conv != CONV_R) {
			/* can't do ostream_printf() in case string
			 * contains null character */
			struct chx_string *str = (struct chx_string *)arg;
			for (int i = 0; i < str->len; ++i)
				ostream_putchar(&ss.ostr, str->value[i]);
		} else {
			ostream_show(c, &ss.ostr, arg);
		}
	}

	/* Add padding if necessary.
	 * TODO this should probably count graphemes rather than bytes */
	int written = ss.idx - prev_idx;
	int padding = field_width - written;

	for (int i = 0; i < field_width - written; ++i)
		ostream_putchar(&ss.ostr, pad_char);

	if (indexing == AUTO_IDX)
		++cur_arg_idx;

	++fmt;
	goto normal;

single_end_curly:
	ch = *fmt;
	if (ch == '}') {
		ostream_putchar(&ss.ostr, ch);
		++fmt;
		goto normal;
	}

	cry(c, "format", CHEAX_EVALUE, "encountered single `}' in format string");
	goto error;

finish:
	res = &cheax_nstring(c, ss.buf, ss.idx)->base;

error:
	free(arg_array);
	free(ss.buf);
	return res;
}
