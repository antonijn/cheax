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
#include <string.h>

#include "cinfo.h"
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
	bool allow_splice;
	int lah_buf[MAX_LOOKAHEAD];
	const char *path;
};

static struct chx_value read_value(struct read_info *ri, struct scnr *s, bool consume_final);

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
	ri->allow_splice = false;
	ri->path = path;

	scnr_init(s, strm, MAX_LOOKAHEAD, &ri->lah_buf[0], line, pos);
}

static void
skip_space(struct scnr *s)
{
	for (;;) {
		while (c_isspace(s->ch))
			scnr_adv(s);

		if (s->ch != ';')
			break;

		/* skip comment line */
		scnr_adv(s);
		while (s->ch != '\n' && s->ch != EOF)
			scnr_adv(s);
	}
}

static struct chx_value
read_id(struct read_info *ri, struct scnr *s) /* consume_final = true */
{
	struct sostrm ss;
	sostrm_init(&ss, ri->c);

	struct chx_value res = cheax_nil();

	while (c_isid(s->ch))
		ostrm_putc(&ss.strm, scnr_adv(s));

	if (s->ch != EOF && !c_isspace(s->ch) && s->ch != ')') {
		cheax_throwf(ri->c, CHEAX_EREAD, "only whitespace or `)' may follow identifier");
		goto done;
	}

	if (ss.idx == 4 && memcmp(ss.buf, "true", 4) == 0) {
		res = cheax_true();
	} else if (ss.idx == 5 && memcmp(ss.buf, "false", 5) == 0) {
		res = cheax_false();
	} else {
		ostrm_putc(&ss.strm, '\0');
		res = cheax_id(ri->c, ss.buf);
	}

done:
	cheax_free(ri->c, ss.buf);
	return res;
}

static long long
read_digits(struct scnr *s, struct ostrm *ostr, int base, bool *too_big)
{
	long long value = 0;
	bool overflow = false;

	for (;;) {
		int digit = c_todigit(s->ch, base);
		if (digit < 0)
			break;

		ostrm_putc(ostr, scnr_adv(s));

		if (overflow || value > LLONG_MAX / base) {
			overflow = true;
			continue;
		}

		value *= base;

		if (value > LLONG_MAX - digit) {
			overflow = true;
			continue;
		}

		value += digit;
	}

	if (too_big != NULL)
		*too_big = overflow;

	return value;
}

static struct chx_value
read_num(struct read_info *ri, struct scnr *s) /* consume_final = true */
{
	struct sostrm ss;
	sostrm_init(&ss, ri->c);

	struct chx_value res = cheax_nil();

	/*
	 * We optimise (slightly) for integers, whose value we parse as
	 * we read them. For doubles, this isn't really possible, since
	 * we must rely on strtod() after we've read the number.
	 *
	 * Beyond just an optimisation, this also allows us to read
	 * integer formats that aren't standard for strtol(), such as
	 * "0b"-prefixed binary numbers.
	 */

	long long pos_whole_value = 0; /* Value of positive whole part */
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
		} else if (c_isdigit(s->ch)) {
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

	if (s->ch != EOF && !c_isspace(s->ch) && s->ch != ')') {
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

		res = cheax_int((chx_int)pos_whole_value);
		goto done;
	}

	ostrm_putc(&ss.strm, '\0');

	double dval;
	char *endptr;

#if defined(HAVE_STRTOD_L)
	dval = strtod_l(ss.buf, &endptr, get_c_locale());
#elif defined(HAVE_WINDOWS_STRTOD_L)
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

	res = cheax_double(dval);

done:
	cheax_free(ri->c, ss.buf);
	return res;
}

static void
read_bslash(struct read_info *ri, struct scnr *s, struct ostrm *ostr) /* consume_final = true */
{
	/* function expects the backslash itself to have been consumed */

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
		unsigned digits[2];
		for (int i = 0; i < 2; ++i) {
			int x = c_todigit(s->ch, 16);
			if (x < 0) {
				cheax_throwf(ri->c, CHEAX_EREAD, "expected two hex digits after `\\x'");
				return;
			}

			digits[i] = x;
			scnr_adv(s);
		}

		ostrm_putc(ostr, (digits[0] << 4) + digits[1]);
		return;
	}

	if (s->ch == 'u' || s->ch == 'U') {
		int spec = s->ch;
		unsigned digits[8];
		size_t num_digits = (spec == 'u') ? 4 : 8;

		scnr_adv(s);
		for (size_t i = 0; i < num_digits; ++i) {
			int x = c_todigit(s->ch, 16);
			if (x < 0) {
				cheax_throwf(ri->c,
				             CHEAX_EREAD,
				             "expected %zd hex digits after `\\%c'",
				             num_digits,
				             spec);
				return;
			}

			digits[i] = x;
			scnr_adv(s);
		}

		/* code point */
		unsigned cp = 0;
		for (size_t i = 0; i < num_digits; ++i)
			cp = (cp << 4) | digits[i];

		if (cp > 0x10FFFF) {
			cheax_throwf(ri->c, CHEAX_EREAD, "code point out of bounds: U+%08X", cp);
			return;
		}

		ostrm_put_utf8(ostr, cp);
		return;
	}

	cheax_throwf(ri->c, CHEAX_EREAD, "unexpected character after `\\'");
}

static struct chx_value
read_string(struct read_info *ri, struct scnr *s, bool consume_final)
{
	struct sostrm ss;
	sostrm_init(&ss, ri->c);

	struct chx_value res = cheax_nil();

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

	res = cheax_nstring(ri->c, ss.buf, ss.idx);
done:
	cheax_free(ri->c, ss.buf);
	return res;
}

static struct chx_value
read_list(struct read_info *ri, struct scnr *s, bool consume_final)
{
	struct chx_list *lst = NULL;
	struct debug_info info = { ri->path, s->pos, s->line };

	bool did_allow_splice = ri->allow_splice;
	if (ri->bkquote_stack - ri->comma_stack > 0)
		ri->allow_splice = true;

	scnr_adv(s);
	if ((skip_space(s), s->ch) != ')') {
		if (s->ch == EOF)
			goto eof_pad;

		lst = ri->c->gen_debug_info
		    ? &debug_list(ri->c, read_value(ri, s, true), NULL, info)->base
		    : cheax_list(ri->c, read_value(ri, s, true), NULL).data.as_list;
		cheax_ft(ri->c, pad);

		struct chx_list **next = &lst->next;
		while ((skip_space(s), s->ch) != ')') {
			if (s->ch == EOF)
				goto eof_pad;

			*next = cheax_list(ri->c, read_value(ri, s, true), NULL).data.as_list;
			cheax_ft(ri->c, pad);
			next = &(*next)->next;
		}
	}

	ri->allow_splice = did_allow_splice;

	if (consume_final)
		scnr_adv(s);

	struct chx_value res;
	res.type = CHEAX_LIST;
	res.data.as_list = lst;
	return res;
eof_pad:
	cheax_throwf(ri->c, CHEAX_EEOF, "unexpected end-of-file in S-expression");
pad:
	return cheax_nil();
}

static struct chx_value
read_value(struct read_info *ri, struct scnr *s, bool consume_final)
{
	skip_space(s);

	if (s->ch == '-') {
		scnr_adv(s);
		bool is_num = false;

		if (c_isdigit(s->ch)) {
			is_num = true;
		} else if (s->ch == '.') {
			scnr_adv(s);
			is_num = c_isdigit(s->ch);
			scnr_backup(s, '.');
		}

		scnr_backup(s, '-');

		return is_num ? read_num(ri, s) : read_id(ri, s);
	}

	if (s->ch == '.') {
		scnr_adv(s);
		bool is_num = c_isdigit(s->ch);
		scnr_backup(s, '.');

		return is_num ? read_num(ri, s) : read_id(ri, s);
	}

	if (c_isid_initial(s->ch))
		return read_id(ri, s);

	if (c_isdigit(s->ch))
		return read_num(ri, s);

	if (s->ch == '(')
		return read_list(ri, s, consume_final);

	if (s->ch == '\'') {
		/* Technically, quote is supposed to be syntactic sugar
		 * for a special form of the kind (quote ...), which is
		 * an S-expression. Therefore, we _technically_ need to
		 * allow splicing. */
		bool did_allow_splice = ri->allow_splice;
		if (ri->bkquote_stack - ri->comma_stack > 0)
			ri->allow_splice = true;

		scnr_adv(s);
		struct chx_value to_quote = read_value(ri, s, consume_final);
		ri->allow_splice = did_allow_splice;
		cheax_ft(ri->c, pad);
		return cheax_quote(ri->c, to_quote);
	}

	if (s->ch == '`') {
		/* See comment above */
		bool did_allow_splice = ri->allow_splice;
		if (ri->bkquote_stack - ri->comma_stack > 0)
			ri->allow_splice = true;

		scnr_adv(s);
		++ri->bkquote_stack;
		struct chx_value to_quote = read_value(ri, s, consume_final);
		--ri->bkquote_stack;
		ri->allow_splice = did_allow_splice;
		cheax_ft(ri->c, pad);
		return cheax_backquote(ri->c, to_quote);
	}

	if (s->ch == ',') {
		if (ri->bkquote_stack == 0) {
			cheax_throwf(ri->c, CHEAX_EREAD, "comma is illegal outside of backquotes");
			return cheax_nil();
		}
		/* same error, different message */
		if (ri->comma_stack >= ri->bkquote_stack) {
			cheax_throwf(ri->c, CHEAX_EREAD, "more commas than backquotes");
			return cheax_nil();
		}

		scnr_adv(s);
		bool splice = false;
		if (s->ch == '@') {
			if (!ri->allow_splice) {
				cheax_throwf(ri->c, CHEAX_EREAD, "invalid splice");
				return cheax_nil();
			}

			splice = true;
			scnr_adv(s);
		}
		++ri->comma_stack;
		struct chx_value to_comma = read_value(ri, s, consume_final);
		--ri->comma_stack;
		cheax_ft(ri->c, pad);
		return (splice ? cheax_splice : cheax_comma)(ri->c, to_comma);
	}

	if (s->ch == '"')
		return read_string(ri, s, consume_final);

	if (s->ch != EOF)
		cheax_throwf(ri->c, CHEAX_EREAD, "unexpected character `%c'", s->ch);
pad:
	return cheax_nil();
}

static struct chx_value
istrm_read_at(CHEAX *c, struct istrm *strm, const char *path, int *line, int *pos)
{
	int ln = (line == NULL) ? 1 : *line;
	int ps =  (pos == NULL) ? 0 : *pos;

	struct read_info ri;
	struct scnr s;
	read_init(&ri, &s, strm, c, path, ln, ps);

	struct chx_value res = read_value(&ri, &s, false);

	if (line != NULL)
		*line = s.line;
	if (pos != NULL)
		*pos = s.pos;

	return res;
}

struct chx_value
cheax_read(CHEAX *c, FILE *infile)
{
	return cheax_read_at(c, infile, "<filename unknown>", NULL, NULL);
}
struct chx_value
cheax_read_at(CHEAX *c, FILE *infile, const char *path, int *line, int *pos)
{
	struct fistrm fs;
	fistrm_init(&fs, infile, c);
	return istrm_read_at(c, &fs.strm, path, line, pos);
}
struct chx_value
cheax_readstr(CHEAX *c, const char *str)
{
	return cheax_readstr_at(c, &str, "<filename unknown>", NULL, NULL);
}
struct chx_value
cheax_readstr_at(CHEAX *c, const char **str, const char *path, int *line, int *pos)
{
	struct sistrm ss;
	sistrm_init(&ss, *str);
	struct chx_value res = istrm_read_at(c, &ss.strm, path, line, pos);
	if (cheax_errno(c) == 0)
		*str = ss.str + ss.idx;
	return res;
}
