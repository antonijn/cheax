/* Copyright (c) 2020, Antonie Blom
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
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <gc.h>

#include "api.h"

enum tok_kind {
	TK_ID, TK_INT, TK_DOUBLE, TK_LPAR, TK_RPAR, TK_QUOTE, TK_STRING, TK_EOF
};

struct tok {
	enum tok_kind kind;
	char lexeme[101];
};

struct lexer {
	CHEAX *c;
	int cur;
	void *info;
	int (*get)(void *info);
};

static void lxadv(struct lexer *lx, struct tok *tk);
static struct chx_value *ast_read(struct lexer *lx, struct tok *tk);

struct sstream {
	const char *str;
	int idx;
};
static int
get_sgetc(void *s)
{
	struct sstream *ss = s;
	char ch = ss->str[ss->idx];
	if (ch == '\0')
		return EOF;

	++ss->idx;
	return ch;
}
static void
lxinits(struct lexer *lx, CHEAX *c, struct tok *tk, struct sstream *s)
{
	lx->c = c;
	lx->info = s;
	lx->get = get_sgetc;
	lx->cur = get_sgetc(s);
	lxadv(lx, tk);
}

static int
get_fgetc(void *f)
{
	return fgetc(f);
}
static inline void
lxinitf(struct lexer *lx, CHEAX *c, struct tok *tk, FILE *f)
{
	lx->c = c;
	lx->info = f;
	lx->get = get_fgetc;
	lx->cur = fgetc(f);
	lxadv(lx, tk);
}

static int
lxadvch(struct lexer *lx)
{
	int res = lx->cur;
	lx->cur = lx->get(lx->info);
	return res;
}

static int
isid(int ch)
{
	return ch != '(' && ch != ')' && !isspace(ch) && isprint(ch);
}

static void
trim_space(struct lexer *lx)
{
	while (isspace(lx->cur))
		lxadvch(lx);

	if (lx->cur == ';') {
		lxadvch(lx);
		while (lx->cur != '\n' && lx->cur != EOF)
			lxadvch(lx);
		trim_space(lx);
	}
}

/* Returns false if string needs to terminate */
static bool
get_string_char(struct lexer *lx, char **dest)
{
	int ch = lx->cur;
	*(*dest)++ = ch;
	lxadvch(lx);

	if (ch == '"')
		return false;

	if (ch == '\n' || ch == EOF) {
		cry(lx->c, "read", CHEAX_EEOF, "Unexpected end of file");
		return false;
	}

	if (ch == '\\') {
		*(*dest)++ = lx->cur;
		lxadvch(lx);
	}

	return true;
}

static void
get_string(struct lexer *lx, struct tok *tk)
{
	char *dest = tk->lexeme;
	*dest++ = lx->cur; /* copy '"' */
	lxadvch(lx);
	while (get_string_char(lx, &dest))
		;
	*dest = '\0';
	tk->kind = TK_STRING;
}

static void
get_num(struct lexer *lx, struct tok *tk)
{
	char *dest = tk->lexeme;
	while (strchr("0123456789abcdefABCDEFxX.", lx->cur))
		*dest++ = lxadvch(lx);
	*dest = '\0';
	tk->kind = strchr(tk->lexeme, '.') ? TK_DOUBLE : TK_INT;
}

static void
get_id(struct lexer *lx, struct tok *tk)
{
	char *dest = tk->lexeme;
	while (isid(lx->cur))
		*dest++ = lxadvch(lx);
	*dest = '\0';
	tk->kind = TK_ID;
}

void
lxadv(struct lexer *lx, struct tok *tk)
{
	trim_space(lx);

	if (lx->cur == '\'') {
		strcpy(tk->lexeme, "'");
		tk->kind = TK_QUOTE;
		lxadvch(lx);
	} else if (lx->cur == '"') {
		get_string(lx, tk);
	} else if (isid(lx->cur)) {
		if (isdigit(lx->cur))
			get_num(lx, tk);
		else
			get_id(lx, tk);
	} else if (lx->cur == '(') {
		strcpy(tk->lexeme, "(");
		tk->kind = TK_LPAR;
		lxadvch(lx);
	} else if (lx->cur == ')') {
		strcpy(tk->lexeme, ")");
		tk->kind = TK_RPAR;
		lxadvch(lx);
	} else if (lx->cur == '\0' || lx->cur == EOF) {
		tk->kind = TK_EOF;
	}
}

struct chx_value *
read_int(struct lexer *lx, struct tok *tk)
{
	int prev_errno = errno;
	long val = strtol(tk->lexeme, NULL, 0);
	int new_errno = errno;
	errno = prev_errno;

	if (new_errno == ERANGE || val > INT_MAX || val < INT_MIN) {
		cry(lx->c, "read", CHEAX_EREAD, "Invalid integer");
		return NULL;
	}

	return &cheax_int(lx->c, val)->base;
}
struct chx_value *
read_double(struct lexer *lx, struct tok *tk)
{
	int prev_errno = errno;
	double val = strtod(tk->lexeme, NULL);
	int new_errno = errno;
	errno = prev_errno;

	if (new_errno == ERANGE) {
		cry(lx->c, "read", CHEAX_EREAD, "Invalid floating point number");
		return NULL;
	}

	return &cheax_double(lx->c, val)->base;
}
struct chx_value *
read_id(struct lexer *lx, struct tok *tk)
{
	return &cheax_id(lx->c, tk->lexeme)->base;
}
struct chx_value *
read_string(struct lexer *lx, struct tok *tk)
{
	/* Enough to hold all characters */
	size_t crude_len = strlen(tk->lexeme);
	char *value = malloc(crude_len);

	char *shift = value;
	for (int i = 1; i + 1 < crude_len; ++i) {
		char ch = tk->lexeme[i];

		if (ch != '\\') {
			*shift++ = ch;
			continue;
		}

		ch = tk->lexeme[++i];
		assert(i + 1 < crude_len);

		switch (ch) {
		case 'n':
			*shift++ = '\n';
			break;
		default:
			*shift++ = ch;
			break;
		}
	}

	size_t act_len = (shift - value);
	struct chx_string *res = cheax_nstring(lx->c, value, act_len);

	free(value);

	return &res->base;
}

struct chx_value *
read_cons(struct lexer *lx, struct tok *tk)
{
	struct chx_list *res = NULL;
	struct chx_list **last = &res;
	lxadv(lx, tk);
	while (tk->kind != TK_RPAR) {
		if (tk->kind == TK_EOF) {
			cry(lx->c, "read", CHEAX_EEOF, "Unexpected end of file");
			return NULL;
		}
		*last = cheax_list(lx->c, ast_read(lx, tk), NULL);
		last = &(*last)->next;
		lxadv(lx, tk);
	}
	return &res->base;
}

static struct chx_value *
ast_read(struct lexer *lx, struct tok *tk)
{
	switch (tk->kind) {
	case TK_ID:
		return read_id(lx, tk);
	case TK_INT:
		return read_int(lx, tk);
	case TK_DOUBLE:
		return read_double(lx, tk);
	case TK_LPAR:
		return read_cons(lx, tk);
	case TK_QUOTE:
		lxadv(lx, tk);
		return &cheax_quote(lx->c, ast_read(lx, tk))->base;
	case TK_STRING:
		return read_string(lx, tk);
	case TK_EOF:
		return NULL;
	default:
		cry(lx->c, "read", CHEAX_EREAD, "Unexpected token");
		return NULL;
	}
}

struct chx_value *
cheax_read(CHEAX *c, FILE *infile)
{
	struct lexer lx;
	struct tok tk;
	lxinitf(&lx, c, &tk, infile);
	return ast_read(&lx, &tk);
}
struct chx_value *
cheax_readstr(CHEAX *c, const char *str)
{
	struct sstream ss = { .str = str, .idx = 0 };
	struct lexer lx;
	struct tok tk;
	lxinits(&lx, c, &tk, &ss);
	return ast_read(&lx, &tk);
}
