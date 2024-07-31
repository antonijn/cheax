/* Copyright (c) 2023, Antonie Blom
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

	cheax_scnr_init_(s, strm, MAX_LOOKAHEAD, &ri->lah_buf[0], line, pos);
}

static void
skip_space(struct scnr *s)
{
	for (;;) {
		while (cheax_isspace_(s->ch))
			cheax_scnr_adv_(s);

		if (s->ch != ';')
			break;

		/* skip comment line */
		cheax_scnr_adv_(s);
		while (s->ch != '\n' && s->ch != EOF)
			cheax_scnr_adv_(s);
	}
}

static struct chx_value
read_id(struct read_info *ri, struct scnr *s) /* consume_final = true */
{
	struct sostrm ss;
	cheax_sostrm_init_(&ss, ri->c);

	struct chx_value res = CHEAX_NIL;

	while (cheax_isid_(s->ch))
		cheax_ostrm_putc_(&ss.strm, cheax_scnr_adv_(s));

	if (s->ch != EOF && !cheax_isspace_(s->ch) && s->ch != ')') {
		cheax_throwf(ri->c, CHEAX_EREAD, "only whitespace or `)' may follow identifier");
		goto done;
	}

	if (ss.idx == 4 && memcmp(ss.buf, "true", 4) == 0) {
		res = cheax_true();
	} else if (ss.idx == 5 && memcmp(ss.buf, "false", 5) == 0) {
		res = cheax_false();
	} else {
		cheax_ostrm_putc_(&ss.strm, '\0');
		res = cheax_id(ri->c, ss.buf);
	}

done:
	cheax_free(ri->c, ss.buf);
	return res;
}

static chx_int
read_digits(struct scnr *s, struct ostrm *ostr, bool neg, int base, bool *too_big)
{
	chx_int value = 0;
	bool overflow = false, ofl, ufl;

	for (;;) {
		int digit = cheax_todigit_(s->ch, base);
		if (digit < 0)
			break;

		cheax_ostrm_putc_(ostr, cheax_scnr_adv_(s));

		ofl = value > CHX_INT_MAX / base;
		ufl = value < CHX_INT_MIN / base;
		if (overflow || (!neg && ofl) || (neg && ufl)) {
			overflow = true;
			continue;
		}

		value *= base;

		ofl = value > CHX_INT_MAX - digit;
		ufl = value < CHX_INT_MIN + digit;
		if ((!neg && ofl) || (!neg && ufl)) {
			overflow = true;
			continue;
		}

		if (!neg)
			value += digit;
		else
			value -= digit;
	}

	if (too_big != NULL)
		*too_big = overflow;

	return value;
}

static struct chx_value
read_num(struct read_info *ri, struct scnr *s) /* consume_final = true */
{
	struct sostrm ss;
	cheax_sostrm_init_(&ss, ri->c);

	struct chx_value res = CHEAX_NIL;

	/*
	 * We optimise (slightly) for integers, whose value we parse as
	 * we read them. For doubles, this isn't really possible, since
	 * we must rely on strtod() after we've read the number.
	 *
	 * Beyond just an optimisation, this also allows us to read
	 * integer formats that aren't standard for strtol(), such as
	 * "0b"-prefixed binary numbers.
	 */

	chx_int whole_value = 0; /* Value of whole part */
	bool negative = false, too_big = false, is_double = false;
	int base = 10;

	if (s->ch == '-') {
		negative = true;
		cheax_ostrm_putc_(&ss.strm, cheax_scnr_adv_(s));
	} else if (s->ch == '+') {
		cheax_ostrm_putc_(&ss.strm, cheax_scnr_adv_(s));
	}

	if (s->ch == '0') {
		cheax_ostrm_putc_(&ss.strm, cheax_scnr_adv_(s));
		if (s->ch == 'x' || s->ch == 'X') {
			base = 16;
			cheax_ostrm_putc_(&ss.strm, cheax_scnr_adv_(s));
		} else if (s->ch == 'b' || s->ch == 'B') {
			base = 2;
			cheax_ostrm_putc_(&ss.strm, cheax_scnr_adv_(s));
		} else if (cheax_isdigit_(s->ch)) {
			base = 8;
		}
	}

	whole_value = read_digits(s, &ss.strm, negative, base, &too_big);

	if (s->ch == '.' && (base == 10 || base == 16)) {
		is_double = true;
		cheax_ostrm_putc_(&ss.strm, cheax_scnr_adv_(s));
		read_digits(s, &ss.strm, false, base, NULL);
	}

	if ((base == 10 && (s->ch == 'e' || s->ch == 'E'))
	 || (base == 16 && (s->ch == 'p' || s->ch == 'P')))
	{
		cheax_ostrm_putc_(&ss.strm, cheax_scnr_adv_(s));

		if (s->ch == '-' || s->ch == '+')
			cheax_ostrm_putc_(&ss.strm, cheax_scnr_adv_(s));

		read_digits(s, &ss.strm, false, base, NULL);
	}

	if (s->ch != EOF && !cheax_isspace_(s->ch) && s->ch != ')') {
		cheax_throwf(ri->c, CHEAX_EREAD, "only whitespace or `)' may follow number");
		goto done;
	}

	if (!is_double) {
		if (too_big) {
			cheax_throwf(ri->c, CHEAX_EREAD, "integer too big");
			goto done;
		}

		res = cheax_int(whole_value);
		goto done;
	}

	cheax_ostrm_putc_(&ss.strm, '\0');

	double dval;
	char *endptr;

#if defined(HAVE_STRTOD_L)
	dval = strtod_l(ss.buf, &endptr, cheax_get_c_locale_());
#elif defined(HAVE_WINDOWS_STRTOD_L)
	dval = _strtod_l(ss.buf, &endptr, cheax_get_c_locale_());
#else
	locale_t prev_locale = uselocale(cheax_get_c_locale_());
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
		cheax_ostrm_putc_(ostr, ch);
		cheax_scnr_adv_(s);
		return;
	}

	if (s->ch == 'x' || s->ch == 'X') {
		cheax_scnr_adv_(s);
		unsigned digits[2];
		for (int i = 0; i < 2; ++i) {
			int x = cheax_todigit_(s->ch, 16);
			if (x < 0) {
				cheax_throwf(ri->c, CHEAX_EREAD, "expected two hex digits after `\\x'");
				return;
			}

			digits[i] = x;
			cheax_scnr_adv_(s);
		}

		cheax_ostrm_putc_(ostr, (digits[0] << 4) + digits[1]);
		return;
	}

	if (s->ch == 'u' || s->ch == 'U') {
		int spec = s->ch;
		unsigned digits[8];
		size_t num_digits = (spec == 'u') ? 4 : 8;

		cheax_scnr_adv_(s);
		for (size_t i = 0; i < num_digits; ++i) {
			int x = cheax_todigit_(s->ch, 16);
			if (x < 0) {
				cheax_throwf(ri->c,
				             CHEAX_EREAD,
				             "expected %zd hex digits after `\\%c'",
				             num_digits,
				             spec);
				return;
			}

			digits[i] = x;
			cheax_scnr_adv_(s);
		}

		/* code point */
		unsigned cp = 0;
		for (size_t i = 0; i < num_digits; ++i)
			cp = (cp << 4) | digits[i];

		if (cp > 0x10FFFF) {
			cheax_throwf(ri->c, CHEAX_EREAD, "code point out of bounds: U+%08X", cp);
			return;
		}

		cheax_ostrm_put_utf_8(ostr, cp);
		return;
	}

	cheax_throwf(ri->c, CHEAX_EREAD, "unexpected character after `\\'");
}

static struct chx_value
read_string(struct read_info *ri, struct scnr *s, bool consume_final)
{
	struct sostrm ss;
	cheax_sostrm_init_(&ss, ri->c);

	struct chx_value res = CHEAX_NIL;

	/* consume initial `"' */
	cheax_scnr_adv_(s);

	while (s->ch != '"') {
		int ch;
		switch ((ch = cheax_scnr_adv_(s))) {
		case '\n':
		case EOF:
			cheax_throwf(ri->c, CHEAX_EREAD, "unexpected string termination");
			goto done;

		case '\\':
			read_bslash(ri, s, &ss.strm);
			cheax_ft(ri->c, done);
			break;
		default:
			cheax_ostrm_putc_(&ss.strm, ch);
			break;
		}
	}

	if (consume_final)
		cheax_scnr_adv_(s);

	res = cheax_nstring(ri->c, ss.buf, ss.idx);
done:
	cheax_free(ri->c, ss.buf);
	return res;
}

static struct chx_value
read_list(struct read_info *ri, struct scnr *s, bool consume_final)
{
	struct chx_list *lst = NULL;
	struct loc_debug_info info = { ri->path, s->pos, s->line };

	bool did_allow_splice = ri->allow_splice;
	if (ri->bkquote_stack - ri->comma_stack > 0)
		ri->allow_splice = true;

	cheax_scnr_adv_(s);
	if ((skip_space(s), s->ch) != ')') {
		if (s->ch == EOF)
			goto eof_pad;

		lst = cheax_loc_debug_list_(ri->c, read_value(ri, s, true), NULL, info);
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
		cheax_scnr_adv_(s);

	struct chx_value res;
	res.type = CHEAX_LIST;
	res.data.as_list = lst;
	return res;
eof_pad:
	cheax_throwf(ri->c, CHEAX_EEOF, "unexpected end-of-file in S-expression");
pad:
	return CHEAX_NIL;
}

static struct chx_value
read_value(struct read_info *ri, struct scnr *s, bool consume_final)
{
	skip_space(s);

	if (s->ch == '-') {
		cheax_scnr_adv_(s);
		bool is_num = false;

		if (cheax_isdigit_(s->ch)) {
			is_num = true;
		} else if (s->ch == '.') {
			cheax_scnr_adv_(s);
			is_num = cheax_isdigit_(s->ch);
			cheax_scnr_backup_(s, '.');
		}

		cheax_scnr_backup_(s, '-');

		return is_num ? read_num(ri, s) : read_id(ri, s);
	}

	if (s->ch == '.') {
		cheax_scnr_adv_(s);
		bool is_num = cheax_isdigit_(s->ch);
		cheax_scnr_backup_(s, '.');

		return is_num ? read_num(ri, s) : read_id(ri, s);
	}

	if (cheax_isid_initial_(s->ch))
		return read_id(ri, s);

	if (cheax_isdigit_(s->ch))
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

		cheax_scnr_adv_(s);
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

		cheax_scnr_adv_(s);
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
			return CHEAX_NIL;
		}
		/* same error, different message */
		if (ri->comma_stack >= ri->bkquote_stack) {
			cheax_throwf(ri->c, CHEAX_EREAD, "more commas than backquotes");
			return CHEAX_NIL;
		}

		cheax_scnr_adv_(s);
		bool splice = false;
		if (s->ch == '@') {
			if (!ri->allow_splice) {
				cheax_throwf(ri->c, CHEAX_EREAD, "invalid splice");
				return CHEAX_NIL;
			}

			splice = true;
			cheax_scnr_adv_(s);
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
	return CHEAX_NIL;
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
	ASSERT_NOT_NULL("read_at", infile, CHEAX_NIL);

	struct fistrm fs;
	cheax_fistrm_init_(&fs, infile, c);
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
	ASSERT_NOT_NULL("readstr_at", str, CHEAX_NIL);
	ASSERT_NOT_NULL("readstr_at", *str, CHEAX_NIL);

	struct sistrm ss;
	cheax_sistrm_init_(&ss, *str);
	struct chx_value res = istrm_read_at(c, &ss.strm, path, line, pos);
	if (cheax_errno(c) == 0)
		*str = ss.str + ss.idx;
	return res;
}
