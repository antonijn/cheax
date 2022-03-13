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

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>

#include "core.h"
#include "err.h"
#include "loc.h"
#include "setup.h"
#include "strm.h"

/*
 * We might need two characters of lookahead for atoms
 * beginning with `.-', which could thereafter still be either numbers
 * or identifiers.
 */
#define MAX_LOOKAHEAD 2

struct read_info {
	CHEAX *c;
	int bkquote_stack, comma_stack;
	int lah_buf[MAX_LOOKAHEAD];
	const char *path;
};

static struct chx_value *read_value(struct read_info *ri, struct scnr *s, bool consume_final);

static void
read_init(struct read_info *ri,
          struct scnr *s,
          struct istrm *strm,
          CHEAX *c,
          const char *path,
          int line,
          int pos)
{
	ri->c = c;
	ri->bkquote_stack = ri->comma_stack = 0;
	ri->path = path;

	scnr_init(s, strm, MAX_LOOKAHEAD, &ri->lah_buf[0], line, pos);
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
skip_space(struct scnr *s)
{
	for (;;) {
		while (isspace(s->ch))
			scnr_adv(s);

		if (s->ch != ';')
			break;

		scnr_adv(s);
		while (s->ch != '\n' && s->ch != EOF)
			scnr_adv(s);
	}
}

static struct chx_value *
read_id(struct read_info *ri, struct scnr *s) /* consume_final = true */
{
	struct sostrm ss;
	sostrm_init(&ss, ri->c);

	struct chx_value *res = NULL;

	while (is_id(s->ch))
		ostrm_putc(&ss.strm, scnr_adv(s));

	if (s->ch != EOF && !isspace(s->ch) && s->ch != ')') {
		cheax_throwf(ri->c, CHEAX_EREAD, "only whitespace or `)' may follow identifier");
		goto done;
	}

	if (ss.idx == 4 && memcmp(ss.buf, "true", 4) == 0) {
		res = &cheax_true(ri->c)->base;
	} else if (ss.idx == 5 && memcmp(ss.buf, "false", 5) == 0) {
		res = &cheax_false(ri->c)->base;
	} else {
		ostrm_putc(&ss.strm, '\0');
		res = &cheax_id(ri->c, ss.buf)->base;
	}

done:
	cheax_free(ri->c, ss.buf);
	return res;
}

static int_least64_t
read_digits(struct scnr *s, struct ostrm *ostr, int base, bool *too_big)
{
	int_least64_t value = 0;
	char max_digit = (base <= 10) ? '0' + base - 1 : '9';
	bool overflow = false;

	for (;;) {
		int digit;
		if (s->ch >= '0' && s->ch <= max_digit)
			digit = s->ch - '0';
		else if (base == 16 && s->ch >= 'A' && s->ch <= 'F')
			digit = s->ch - 'A' + 10;
		else if (base == 16 && s->ch >= 'a' && s->ch <= 'f')
			digit = s->ch - 'a' + 10;
		else
			break;

		ostrm_putc(ostr, scnr_adv(s));

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
read_num(struct read_info *ri, struct scnr *s) /* consume_final = true */
{
	struct sostrm ss;
	sostrm_init(&ss, ri->c);

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

	if (s->ch == '-') {
		negative = true;
		ostrm_putc(&ss.strm, scnr_adv(s));
	} else if (s->ch == '+') {
		ostrm_putc(&ss.strm, scnr_adv(s));
	}

	if (s->ch == '0') {
		ostrm_putc(&ss.strm, scnr_adv(s));
		if (s->ch == 'x' || s->ch == 'X') {
			base = 16;
			ostrm_putc(&ss.strm, scnr_adv(s));
		} else if (s->ch == 'b' || s->ch == 'B') {
			base = 2;
			ostrm_putc(&ss.strm, scnr_adv(s));
		} else if (isdigit(s->ch)) {
			base = 8;
		}
	}

	pos_whole_value = read_digits(s, &ss.strm, base, &too_big);

	if (s->ch == '.' && (base == 10 || base == 16)) {
		is_double = true;
		ostrm_putc(&ss.strm, scnr_adv(s));
		read_digits(s, &ss.strm, base, NULL);
	}

	if ((base == 10 && (s->ch == 'e' || s->ch == 'E'))
	 || (base == 16 && (s->ch == 'p' || s->ch == 'P')))
	{
		ostrm_putc(&ss.strm, scnr_adv(s));

		if (s->ch == '-' || s->ch == '+')
			ostrm_putc(&ss.strm, scnr_adv(s));

		read_digits(s, &ss.strm, base, NULL);
	}

	if (s->ch != EOF && !isspace(s->ch) && s->ch != ')') {
		cheax_throwf(ri->c, CHEAX_EREAD, "only whitespace or `)' may follow number");
		goto done;
	}

	if (!is_double) {
		if (negative)
			pos_whole_value = -pos_whole_value;

		if (too_big || pos_whole_value > INT_MAX || pos_whole_value < INT_MIN) {
			cheax_throwf(ri->c, CHEAX_EREAD, "integer too big");
			goto done;
		}

		res = &cheax_int(ri->c, (int)pos_whole_value)->base;
		goto done;
	}

	ostrm_putc(&ss.strm, '\0');

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
		cheax_throwf(ri->c, CHEAX_EREAD, "unexpected strtod() error");
		goto done;
	}

	res = &cheax_double(ri->c, dval)->base;

done:
	cheax_free(ri->c, ss.buf);
	return res;
}

static void
read_bslash(struct read_info *ri, struct scnr *s, struct ostrm *ostr) /* consume_final = true */
{
	/* I expect the backslash itself to have been consumed */

	int ch = -1;
	switch (s->ch) {
	case 'n':  ch = '\n'; break;
	case 'r':  ch = '\r'; break;
	case '\\': ch = '\\'; break;
	case '0':  ch = '\0'; break;
	case 't':  ch = '\t'; break;
	case '\'': ch = '\''; break;
	case '"':  ch = '"';  break;
	}

	if (ch != -1) {
		ostrm_putc(ostr, ch);
		scnr_adv(s);
		return;
	}

	if (s->ch == 'x' || s->ch == 'X') {
		scnr_adv(s);
		int digits[2];
		for (int i = 0; i < 2; ++i) {
			if (s->ch >= '0' && s->ch <= '9') {
				digits[i] = s->ch - '0';
			} else if (s->ch >= 'A' && s->ch <= 'F') {
				digits[i] = s->ch - 'A' + 10;
			} else if (s->ch >= 'a' && s->ch <= 'f') {
				digits[i] = s->ch - 'a' + 10;
			} else {
				cheax_throwf(ri->c, CHEAX_EREAD, "expected two hex digits after `\\x'");
				return;
			}

			scnr_adv(s);
		}

		ostrm_putc(ostr, (digits[0] << 4) + digits[1]);
		return;
	}

	/* TODO maybe uXXXX expressions */

	cheax_throwf(ri->c, CHEAX_EREAD, "unexpected character after `\\'");
}

static struct chx_value *
read_string(struct read_info *ri, struct scnr *s, bool consume_final)
{
	struct sostrm ss;
	sostrm_init(&ss, ri->c);

	struct chx_value *res = NULL;

	/* consume initial `"' */
	scnr_adv(s);

	while (s->ch != '"') {
		int ch;
		switch ((ch = scnr_adv(s))) {
		case '\n':
		case EOF:
			cheax_throwf(ri->c, CHEAX_EREAD, "unexpected string termination");
			goto done;

		case '\\':
			read_bslash(ri, s, &ss.strm);
			cheax_ft(ri->c, done);
			break;
		default:
			ostrm_putc(&ss.strm, ch);
			break;
		}
	}

	if (consume_final)
		scnr_adv(s);

	res = &cheax_nstring(ri->c, ss.buf, ss.idx)->base;
done:
	cheax_free(ri->c, ss.buf);
	return res;
}

static struct chx_value *
read_list(struct read_info *ri, struct scnr *s, bool consume_final)
{
	struct chx_list *lst = NULL;
	struct debug_info info = { ri->path, s->pos, s->line };

	scnr_adv(s);
	if ((skip_space(s), s->ch) != ')') {
		if (s->ch == EOF)
			goto eof_pad;

		lst = ri->c->gen_debug_info
		    ? &debug_list(ri->c, read_value(ri, s, true), NULL, info)->base
		    : cheax_list(ri->c, read_value(ri, s, true), NULL);
		cheax_ft(ri->c, pad);

		struct chx_list **next = &lst->next;
		while ((skip_space(s), s->ch) != ')') {
			if (s->ch == EOF)
				goto eof_pad;

			*next = cheax_list(ri->c, read_value(ri, s, true), NULL);
			cheax_ft(ri->c, pad);
			next = &(*next)->next;
		}
	}

	if (consume_final)
		scnr_adv(s);

	return &lst->base;
eof_pad:
	cheax_throwf(ri->c, CHEAX_EEOF, "unexpected end-of-file in S-expression");
pad:
	return NULL;
}

static struct chx_value *
read_value(struct read_info *ri, struct scnr *s, bool consume_final)
{
	skip_space(s);

	if (s->ch == '-') {
		scnr_adv(s);
		bool is_num = false;

		if (isdigit(s->ch)) {
			is_num = true;
		} else if (s->ch == '.') {
			scnr_adv(s);
			is_num = isdigit(s->ch);
			scnr_backup(s, '.');
		}

		scnr_backup(s, '-');

		return is_num ? read_num(ri, s) : read_id(ri, s);
	}

	if (s->ch == '.') {
		scnr_adv(s);
		bool is_num = isdigit(s->ch);
		scnr_backup(s, '.');

		return is_num ? read_num(ri, s) : read_id(ri, s);
	}

	if (is_initial_id(s->ch))
		return read_id(ri, s);

	if (isdigit(s->ch))
		return read_num(ri, s);

	if (s->ch == '(')
		return read_list(ri, s, consume_final);

	if (s->ch == '\'') {
		scnr_adv(s);
		struct chx_value *to_quote = read_value(ri, s, consume_final);
		cheax_ft(ri->c, pad);
		return &cheax_quote(ri->c, to_quote)->base;
	}

	if (s->ch == '`') {
		scnr_adv(s);
		++ri->bkquote_stack;
		struct chx_value *to_quote = read_value(ri, s, consume_final);
		--ri->bkquote_stack;
		cheax_ft(ri->c, pad);
		return &cheax_backquote(ri->c, to_quote)->base;
	}

	if (s->ch == ',') {
		if (ri->bkquote_stack == 0) {
			cheax_throwf(ri->c, CHEAX_EREAD, "comma is illegal outside of backquotes");
			return NULL;
		}
		/* same error, different message */
		if (ri->comma_stack >= ri->bkquote_stack) {
			cheax_throwf(ri->c, CHEAX_EREAD, "more commas than backquotes");
			return NULL;
		}

		scnr_adv(s);
		++ri->comma_stack;
		struct chx_value *to_comma = read_value(ri, s, consume_final);
		--ri->comma_stack;
		cheax_ft(ri->c, pad);
		return &cheax_comma(ri->c, to_comma)->base;
	}

	if (s->ch == '"')
		return read_string(ri, s, consume_final);

	if (s->ch != EOF)
		cheax_throwf(ri->c, CHEAX_EREAD, "unexpected character `%c'", s->ch);
pad:
	return NULL;
}

static struct chx_value *
istrm_read_at(CHEAX *c, struct istrm *strm, const char *path, int *line, int *pos)
{
	int ln = (line == NULL) ? 1 : *line;
	int ps =  (pos == NULL) ? 0 : *pos;

	struct read_info ri;
	struct scnr s;
	read_init(&ri, &s, strm, c, path, ln, ps);

	struct chx_value *res = read_value(&ri, &s, false);

	if (line != NULL)
		*line = s.line;
	if (pos != NULL)
		*pos = s.pos;

	return res;
}

struct chx_value *
cheax_read(CHEAX *c, FILE *infile)
{
	return cheax_read_at(c, infile, "<filename unknown>", NULL, NULL);
}
struct chx_value *
cheax_read_at(CHEAX *c, FILE *infile, const char *path, int *line, int *pos)
{
	struct fistrm fs;
	fistrm_init(&fs, infile, c);
	return istrm_read_at(c, &fs.strm, path, line, pos);
}
struct chx_value *
cheax_readstr(CHEAX *c, const char *str)
{
	return cheax_readstr_at(c, &str, "<filename unknown>", NULL, NULL);
}
struct chx_value *
cheax_readstr_at(CHEAX *c, const char **str, const char *path, int *line, int *pos)
{
	struct sistrm ss;
	sistrm_init(&ss, *str);
	struct chx_value *res = istrm_read_at(c, &ss.strm, path, line, pos);
	if (cheax_errno(c) == 0)
		*str = ss.str + ss.idx;
	return res;
}
