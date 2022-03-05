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
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>

#include "api.h"
#include "config.h"
#include "loc.h"
#include "stream.h"

/*
 * We might need two characters of lookahead for atoms
 * beginning with `.-', which could thereafter still be either numbers
 * or identifiers.
 */
#define MAX_LOOKAHEAD 2

struct reader {
	CHEAX *c;
	struct istream *istr;
	int ch;
	int bkquote_stack, comma_stack;

	int buf[MAX_LOOKAHEAD];
	int lah; /* actual lookahead */
};

static int
rdr_advch(struct reader *rdr)
{
	int res = rdr->ch;
	if (res != EOF) {
		int pop = 0;
		for (int i = rdr->lah - 1; i >= 0; --i) {
			int next_pop = rdr->buf[i];
			rdr->buf[i] = pop;
			pop = next_pop;
		}

		if (rdr->lah == 0) {
			rdr->ch = istream_getchar(rdr->istr);
		} else {
			--rdr->lah;
			rdr->ch = pop;
		}

	}
	return res;
}

static int
rdr_backup_to(struct reader *rdr, int c)
{
	if (rdr->lah >= MAX_LOOKAHEAD) {
		cry(rdr->c, "read", CHEAX_EREAD, "rdr_backup_to() called too often");
		return EOF;
	}

	++rdr->lah;

	int push = rdr->ch;
	for (int i = 0; i < rdr->lah; ++i) {
		int next_push = rdr->buf[i];
		rdr->buf[i] = push;
		push = next_push;
	}
	return rdr->ch = c;
}

static void
rdr_init(struct reader *rdr, struct istream *istr, CHEAX *c)
{
	rdr->c = c;
	rdr->istr = istr;
	rdr->ch = 0;
	rdr->bkquote_stack = 0;
	rdr->comma_stack = 0;

	memset(rdr->buf, 0, MAX_LOOKAHEAD * sizeof(rdr->buf[0]));
	rdr->lah = 0;

	rdr_advch(rdr);
}

static int
is_id(int ch)
{
	return ch != '(' && ch != ')'
	    && !isspace(ch) && isprint(ch)
	    && ch != '\'' && ch != '`' && ch != ',' && ch != '"' && ch != ';';
}

static int
is_initial_id(int ch)
{
	return is_id(ch) && !isdigit(ch);
}

static void
skip_space(struct reader *rdr)
{
	while (isspace(rdr->ch))
		rdr_advch(rdr);

	if (rdr->ch == ';') {
		rdr_advch(rdr);
		while (rdr->ch != '\n' && rdr->ch != EOF)
			rdr_advch(rdr);
		skip_space(rdr);
	}
}

static struct chx_value *
read_id(struct reader *rdr) /* consume_final = true */
{
	struct sostream ss;
	sostream_init(&ss, rdr->c);

	struct chx_value *res = NULL;

	while (is_id(rdr->ch))
		ostream_putchar(&ss.ostr, rdr_advch(rdr));

	if (rdr->ch != EOF && !isspace(rdr->ch) && rdr->ch != ')') {
		cry(rdr->c, "read", CHEAX_EREAD, "only whitespace or `)' may follow identifier");
		goto done;
	}

	if (ss.idx == 4 && memcmp(ss.buf, "true", 4) == 0) {
		res = &cheax_true(rdr->c)->base;
	} else if (ss.idx == 5 && memcmp(ss.buf, "false", 5) == 0) {
		res = &cheax_false(rdr->c)->base;
	} else {
		ostream_putchar(&ss.ostr, '\0');
		res = &cheax_id(rdr->c, ss.buf)->base;
	}

done:
	free(ss.buf);
	return res;
}

static int_least64_t
read_digits(struct reader *rdr,
            struct ostream *ostr,
            int base,
            bool *too_big)
{
	int_least64_t value = 0;
	char max_digit = (base <= 10) ? '0' + base - 1 : '9';
	bool overflow = false;

	for (;;) {
		int digit;
		if (rdr->ch >= '0' && rdr->ch <= max_digit)
			digit = rdr->ch - '0';
		else if (base == 16 && rdr->ch >= 'A' && rdr->ch <= 'F')
			digit = rdr->ch - 'A' + 10;
		else if (base == 16 && rdr->ch >= 'a' && rdr->ch <= 'f')
			digit = rdr->ch - 'a' + 10;
		else
			break;

		ostream_putchar(ostr, rdr_advch(rdr));

		if (overflow || value > INT_LEAST64_MAX / base) {
			overflow = true;
			continue;
		}

		value *= base;

		if (value > INT_LEAST64_MAX - digit) {
			overflow = true;
			continue;
		}

		value += digit;
	}

	if (too_big != NULL)
		*too_big = overflow;

	return value;
}

static struct chx_value *
read_num(struct reader *rdr) /* consume_final = true */
{
	struct sostream ss;
	sostream_init(&ss, rdr->c);

	struct chx_value *res = NULL;

	/*
	 * We optimise (slightly) for integers, whose value we parse as
	 * we read them. For doubles, this isn't really possible, since
	 * we must rely on strtod() after we've read the number.
	 *
	 * Beyond just an optimisation, this also allows us to read
	 * integer formats that aren't standard for strtol(), such as
	 * "0b"-prefixed binary numbers.
	 */

	int_least64_t pos_whole_value = 0; /* Value of positive whole part */
	bool negative = false, too_big = false, is_double = false;
	int base = 10;

	if (rdr->ch == '-') {
		negative = true;
		ostream_putchar(&ss.ostr, rdr_advch(rdr));
	} else if (rdr->ch == '+') {
		ostream_putchar(&ss.ostr, rdr_advch(rdr));
	}

	if (rdr->ch == '0') {
		ostream_putchar(&ss.ostr, rdr_advch(rdr));
		if (rdr->ch == 'x' || rdr->ch == 'X') {
			base = 16;
			ostream_putchar(&ss.ostr, rdr_advch(rdr));
		} else if (rdr->ch == 'b' || rdr->ch == 'B') {
			base = 2;
			ostream_putchar(&ss.ostr, rdr_advch(rdr));
		} else if (isdigit(rdr->ch)) {
			base = 8;
		}
	}

	pos_whole_value = read_digits(rdr, &ss.ostr, base, &too_big);

	if (rdr->ch == '.' && (base == 10 || base == 16)) {
		is_double = true;
		ostream_putchar(&ss.ostr, rdr_advch(rdr));
		read_digits(rdr, &ss.ostr, base, NULL);
	}

	if ((base == 10 && (rdr->ch == 'e' || rdr->ch == 'E'))
	 || (base == 16 && (rdr->ch == 'p' || rdr->ch == 'P')))
	{
		ostream_putchar(&ss.ostr, rdr_advch(rdr));

		if (rdr->ch == '-' || rdr->ch == '+')
			ostream_putchar(&ss.ostr, rdr_advch(rdr));

		read_digits(rdr, &ss.ostr, base, NULL);
	}

	if (rdr->ch != EOF && !isspace(rdr->ch) && rdr->ch != ')') {
		cry(rdr->c, "read", CHEAX_EREAD, "only whitespace or `)' may follow number");
		goto done;
	}

	if (!is_double) {
		if (negative)
			pos_whole_value = -pos_whole_value;

		if (too_big || pos_whole_value > INT_MAX || pos_whole_value < INT_MIN) {
			cry(rdr->c, "read", CHEAX_EREAD, "integer too big");
			goto done;
		}

		res = &cheax_int(rdr->c, (int)pos_whole_value)->base;
		goto done;
	}

	ostream_putchar(&ss.ostr, '\0');

	double dval;
	char *endptr;

#if defined(HAS_STRTOD_L)
	dval = strtod_l(ss.buf, &endptr, get_c_locale());
#elif defined(HAS_STUPID_WINDOWS_STRTOD_L)
	dval = _strtod_l(ss.buf, &endptr, get_c_locale());
#else
	locale_t prev_locale = uselocale(get_c_locale());
	dval = strtod(ss.buf, &endptr);
	uselocale(prev_locale);
#endif

	if (*endptr != '\0') {
		cry(rdr->c, "read", CHEAX_EREAD, "unexpected strtod() error");
		goto done;
	}

	res = &cheax_double(rdr->c, dval)->base;

done:
	free(ss.buf);
	return res;
}

static void
read_bslash_expr(struct reader *rdr, struct ostream *ostr) /* consume_final = true */
{
	/* I expect the backslash itself to have been consumed */

	int ch = -1;
	switch (rdr->ch) {
	case 'n':  ch = '\n'; break;
	case 'r':  ch = '\r'; break;
	case '\\': ch = '\\'; break;
	case '0':  ch = '\0'; break;
	case 't':  ch = '\t'; break;
	case '\'': ch = '\''; break;
	case '"':  ch = '"';  break;
	}

	if (ch != -1) {
		ostream_putchar(ostr, ch);
		rdr_advch(rdr);
		return;
	}

	if (rdr->ch == 'x' || rdr->ch == 'X') {
		rdr_advch(rdr);
		int digits[2];
		for (int i = 0; i < 2; ++i) {
			if (rdr->ch >= '0' && rdr->ch <= '9') {
				digits[i] = rdr->ch - '0';
			} else if (rdr->ch >= 'A' && rdr->ch <= 'F') {
				digits[i] = rdr->ch - 'A' + 10;
			} else if (rdr->ch >= 'a' && rdr->ch <= 'f') {
				digits[i] = rdr->ch - 'a' + 10;
			} else {
				cry(rdr->c, "read", CHEAX_EREAD, "expected two hex digits after `\\x'");
				return;
			}

			rdr_advch(rdr);
		}

		ostream_putchar(ostr, (digits[0] << 4) + digits[1]);
		return;
	}

	/* TODO maybe uXXXX expressions */

	cry(rdr->c, "read", CHEAX_EREAD, "unexpected character after `\\'");
}

static struct chx_value *
read_string(struct reader *rdr, int consume_final)
{
	struct sostream ss;
	sostream_init(&ss, rdr->c);

	struct chx_value *res = NULL;

	/* consume initial `"' */
	rdr_advch(rdr);

	while (rdr->ch != '"') {
		int ch;
		switch ((ch = rdr_advch(rdr))) {
		case '\n':
		case EOF:
			cry(rdr->c, "read", CHEAX_EREAD, "unexpected string termination");
			goto done;

		case '\\':
			read_bslash_expr(rdr, &ss.ostr);
			cheax_ft(rdr->c, done);
			break;
		default:
			ostream_putchar(&ss.ostr, ch);
			break;
		}
	}

	if (consume_final)
		rdr_advch(rdr);

	res = &cheax_nstring(rdr->c, ss.buf, ss.idx)->base;

done:
	free(ss.buf);
	return res;
}

static struct chx_value *
rdr_read(struct reader *rdr, bool consume_final)
{
	skip_space(rdr);

	if (rdr->ch == '-') {
		rdr_advch(rdr);
		bool is_num = false;

		if (isdigit(rdr->ch)) {
			is_num = true;
		} else if (rdr->ch == '.') {
			rdr_advch(rdr);
			is_num = isdigit(rdr->ch);
			rdr_backup_to(rdr, '.');
		}

		rdr_backup_to(rdr, '-');

		return is_num ? read_num(rdr) : read_id(rdr);
	}

	if (rdr->ch == '.') {
		rdr_advch(rdr);
		bool is_num = isdigit(rdr->ch);
		rdr_backup_to(rdr, '.');

		return is_num ? read_num(rdr) : read_id(rdr);
	}

	if (is_initial_id(rdr->ch))
		return read_id(rdr);

	if (isdigit(rdr->ch))
		return read_num(rdr);

	if (rdr->ch == '(') {
		struct chx_list *lst = NULL;
		struct chx_list **next = &lst;

		rdr_advch(rdr);
		while (rdr->ch != ')') {
			if (rdr->ch == EOF) {
				cry(rdr->c, "read", CHEAX_EEOF, "unexpected end-of-file in s-expression");
				return NULL;
			}

			*next = cheax_list(rdr->c, rdr_read(rdr, true), NULL);
			cheax_ft(rdr->c, pad);
			next = &(*next)->next;

			skip_space(rdr);
		}

		if (consume_final)
			rdr_advch(rdr);

		return &lst->base;
	}

	if (rdr->ch == '\'') {
		rdr_advch(rdr);
		struct chx_value *to_quote = rdr_read(rdr, consume_final);
		cheax_ft(rdr->c, pad);
		return &cheax_quote(rdr->c, to_quote)->base;
	}

	if (rdr->ch == '`') {
		rdr_advch(rdr);
		++rdr->bkquote_stack;
		struct chx_value *to_quote = rdr_read(rdr, consume_final);
		--rdr->bkquote_stack;
		cheax_ft(rdr->c, pad);
		return &cheax_backquote(rdr->c, to_quote)->base;
	}

	if (rdr->ch == ',') {
		if (rdr->bkquote_stack == 0) {
			cry(rdr->c, "read", CHEAX_EREAD, "comma is illegal outside of backquotes");
			return NULL;
		}
		/* same error, different message */
		if (rdr->comma_stack >= rdr->bkquote_stack) {
			cry(rdr->c, "read", CHEAX_EREAD, "more commas than backquotes");
			return NULL;
		}

		rdr_advch(rdr);
		++rdr->comma_stack;
		struct chx_value *to_comma = rdr_read(rdr, consume_final);
		--rdr->comma_stack;
		cheax_ft(rdr->c, pad);
		return &cheax_comma(rdr->c, to_comma)->base;
	}

	if (rdr->ch == '"')
		return read_string(rdr, consume_final);

	if (rdr->ch != EOF)
		cry(rdr->c, "read", CHEAX_EREAD, "unexpected character `%c'", rdr->ch);

pad:
	return NULL;
}

struct chx_value *
cheax_read(CHEAX *c, FILE *infile)
{
	struct fistream fs;
	fistream_init(&fs, infile, c);

	struct reader rdr;
	rdr_init(&rdr, &fs.istr, c);

	return rdr_read(&rdr, false);
}
struct chx_value *
cheax_readstr(CHEAX *c, const char *str)
{
	struct sistream ss;
	sistream_init(&ss, str);

	struct reader rdr;
	rdr_init(&rdr, &ss.istr, c);

	return rdr_read(&rdr, false);
}
