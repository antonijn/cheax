/* Copyright (c) 2016, Antonie Blom
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

#include "read.h"

#include <string.h>
#include <gc.h>
#include <ctype.h>

static int lxadvch(struct lexer *lx)
{
	int res = lx->cur;
	lx->cur = fgetc(lx->f);
	return res;
}

static int isid(int ch)
{
	return ch != '(' && ch != ')' && !isspace(ch) && isprint(ch);
}

static void trim_space(struct lexer *lx)
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

static void get_num(struct lexer *lx, struct tok *tk)
{
	char *dest = tk->lexeme;
	while (strchr("0123456789abcdefABCDEFxX.", lx->cur))
		*dest++ = lxadvch(lx);
	*dest = '\0';
	tk->kind = strchr(tk->lexeme, '.') ? TK_DOUBLE : TK_INT;
}

static void get_id(struct lexer *lx, struct tok *tk)
{
	char *dest = tk->lexeme;
	while (isid(lx->cur))
		*dest++ = lxadvch(lx);
	*dest = '\0';
	tk->kind = TK_ID;
}

void lxadv(struct lexer *lx, struct tok *tk)
{
	trim_space(lx);

	if (lx->cur == '\'') {
		strcpy(tk->lexeme, "'");
		tk->kind = TK_QUOTE;
		lxadvch(lx);
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


static struct chx_value *quote_value(struct chx_value *v)
{
	struct chx_quote *res = GC_MALLOC(sizeof(struct chx_quote));
	res->base.kind = VK_QUOTE;
	res->value = v;
	return &res->base;
}

struct chx_value *read_int(struct lexer *lx, struct tok *tk)
{
	struct chx_int *res = GC_MALLOC(sizeof(struct chx_int));
	res->base.kind = VK_INT;
	res->value = strtol(tk->lexeme, NULL, 0);
	return &res->base;
}
struct chx_value *read_double(struct lexer *lx, struct tok *tk)
{
	struct chx_double *res = GC_MALLOC(sizeof(struct chx_double));
	res->base.kind = VK_DOUBLE;
	res->value = strtod(tk->lexeme, NULL);
	return &res->base;
}
struct chx_value *read_id(struct lexer *lx, struct tok *tk)
{
	struct chx_id *res = GC_MALLOC(sizeof(struct chx_id));
	res->base.kind = VK_ID;
	res->id = GC_MALLOC(sizeof(char) * (strlen(tk->lexeme) + 1));
	strcpy((char *)res->id, tk->lexeme);
	return &res->base;
}

struct chx_value *read_cons(struct lexer *lx, struct tok *tk)
{
	struct chx_cons *res = NULL;
	struct chx_cons **last = &res;
	lxadv(lx, tk);
	while (tk->kind != TK_RPAR) {
		*last = cheax_cons(ast_read(lx, tk), NULL);
		last = &(*last)->next;
		lxadv(lx, tk);
	}
	return &res->base;
}

struct chx_value *ast_read(struct lexer *lx, struct tok *tk)
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
		return quote_value(ast_read(lx, tk));
	case TK_EOF:
		return NULL;
	default:
		fprintf(stderr, "Unexpected token: '%s\n'", tk->lexeme);
		return NULL;
	}
}

struct chx_value *cheax_read(FILE *infile)
{
	struct lexer lx;
	struct tok tk;
	lxinit(&lx, &tk, infile);
	return ast_read(&lx, &tk);
}
