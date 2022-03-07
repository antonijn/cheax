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
#include "print.h"
#include "stream.h"

static void
ostream_show_sym(CHEAX *c, struct ostream *s, struct full_sym *fs)
{
	struct chx_sym *sym = &fs->sym;
	if (sym->get == NULL) {
		ostream_printf(s, ";%s", fs->name);
		return;
	}

	const char *decl = (sym->set == NULL) ? "def" : "var";
	ostream_printf(s, "(%s %s ", decl, fs->name);
	ostream_show(c, s, sym->get(c, sym));
	ostream_putchar(s, ')');
}

static void
ostream_show_basic_type(CHEAX *c, struct ostream *s, struct chx_value *val)
{
	struct chx_env *env;
	struct chx_ext_func *macro;

	int ty = cheax_resolve_type(c, cheax_type_of(val));
	switch (ty) {
	case CHEAX_NIL:
		ostream_printf(s, "()");
		break;
	case CHEAX_INT:
		ostream_printf(s, "%d", ((struct chx_int *)val)->value);
		break;
	case CHEAX_DOUBLE:
		ostream_printf(s, "%f", ((struct chx_double *)val)->value);
		break;
	case CHEAX_BOOL:
		ostream_printf(s, "%s", ((struct chx_int *)val)->value ? "true" : "false");
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
	case CHEAX_MACRO:
		ostream_putchar(s, '(');
		struct chx_func *func = (struct chx_func *)val;
		if (ty == CHEAX_FUNC)
			ostream_printf(s, "fn ");
		else
			ostream_printf(s, "macro ");
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
		for (size_t i = 0; i < string->len; ++i) {
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
		macro = (struct chx_ext_func *)val;
		if (macro->name == NULL)
			ostream_printf(s, "[built-in function]");
		else
			ostream_printf(s, "%s", macro->name);
		break;
	case CHEAX_USER_PTR:
		ostream_printf(s, "%p", ((struct chx_user_ptr *)val)->value);
		break;
	case CHEAX_ENV:
		env = (struct chx_env *)val;
		while (env->is_bif) {
			if (env->value.bif[1] == NULL) {
				env = env->value.bif[0];
				continue;
			}

			ostream_putchar(s, '(');
			ostream_show(c, s, &env->value.bif[1]->base);
			ostream_printf(s, "\n");
			ostream_show(c, s, &env->value.bif[0]->base);
			ostream_putchar(s, ')');
			return;
		}

		ostream_printf(s, "((fn ()");

		struct rb_iter it;
		rb_iter_init(&it);
		for (struct full_sym *fs = rb_iter_first(&it, &env->value.norm.syms);
		     fs != NULL;
		     fs = rb_iter_next(&it))
		{
			ostream_putchar(s, '\n');
			ostream_show_sym(c, s, fs);
		}
		ostream_printf(s, "\n(env)))");
		break;
	}
}

void
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
