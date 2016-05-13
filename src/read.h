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

#ifndef READ_H
#define READ_H

#include <cheax.h>
#include <stdlib.h>
#include <stdio.h>

#include "eval.h"

enum tok_kind {
	TK_ID, TK_INT, TK_DOUBLE, TK_LPAR, TK_RPAR, TK_QUOTE, TK_EOF
};

struct tok {
	enum tok_kind kind;
	char lexeme[101];
};

struct lexer {
	FILE *f;
	int cur;
};

void lxadv(struct lexer *lx, struct tok *tk);

static inline void lxinit(struct lexer *lx, struct tok *tk, FILE *f)
{
	lx->f = f;
	lx->cur = fgetc(f);
	lxadv(lx, tk);
}

struct chx_value *ast_read(struct lexer *lx, struct tok *tk);

#endif
