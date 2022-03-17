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

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "cinfo.h"
#include "core.h"
#include "err.h"
#include "print.h"
#include "strm.h"

static void
ostrm_show_sym(CHEAX *c, struct ostrm *s, struct full_sym *fs)
{
	struct chx_sym *sym = &fs->sym;
	if (sym->get == NULL)
		ostrm_printf(s, ";%s", fs->name);
	else
		ostrm_printf(s, "(%s %s)", (sym->set == NULL) ? "def" : "var", fs->name);
}

static void
ostrm_show_basic(CHEAX *c, struct ostrm *s, struct chx_value *val)
{
	struct chx_env *env;
	struct chx_ext_func *macro;

	int ty = cheax_resolve_type(c, cheax_type_of(val));
	switch (ty) {
	case CHEAX_NIL:
		ostrm_printf(s, "()");
		break;
	case CHEAX_INT:
		ostrm_printi(s, ((struct chx_int *)val)->value, 0, 0, 'd');
		break;
	case CHEAX_DOUBLE:
		ostrm_printf(s, "%f", ((struct chx_double *)val)->value);
		break;
	case CHEAX_BOOL:
		ostrm_printf(s, "%s", ((struct chx_int *)val)->value ? "true" : "false");
		break;
	case CHEAX_ID:
		ostrm_printf(s, "%s", ((struct chx_id *)val)->id);
		break;
	case CHEAX_LIST:
		ostrm_putc(s, '(');
		for (struct chx_list *list = (struct chx_list *)val; list; list = list->next) {
			ostrm_show(c, s, list->value);
			if (list->next)
				ostrm_putc(s, ' ');
		}
		ostrm_putc(s, ')');
		break;
	case CHEAX_QUOTE:
		ostrm_putc(s, '\'');
		ostrm_show(c, s, ((struct chx_quote *)val)->value);
		break;
	case CHEAX_BACKQUOTE:
		ostrm_putc(s, '`');
		ostrm_show(c, s, ((struct chx_quote *)val)->value);
		break;
	case CHEAX_COMMA:
		ostrm_putc(s, ',');
		ostrm_show(c, s, ((struct chx_quote *)val)->value);
		break;
	case CHEAX_SPLICE:
		ostrm_printf(s, ",@");
		ostrm_show(c, s, ((struct chx_quote *)val)->value);
		break;
	case CHEAX_FUNC:
	case CHEAX_MACRO:
		ostrm_putc(s, '(');
		struct chx_func *func = (struct chx_func *)val;
		if (ty == CHEAX_FUNC)
			ostrm_printf(s, "fn ");
		else
			ostrm_printf(s, "macro ");
		ostrm_show(c, s, func->args);
		struct chx_list *body = func->body;
		for (; body; body = body->next) {
			ostrm_printf(s, "\n  ");
			ostrm_show(c, s, body->value);
		}
		ostrm_putc(s, ')');
		break;
	case CHEAX_STRING:
		ostrm_putc(s, '"');
		struct chx_string *string = (struct chx_string *)val;
		for (size_t i = 0; i < string->len; ++i) {
			char ch = string->value[i];
			if (ch == '"')
				ostrm_printf(s, "\\\"");
			else if (c_isprint(ch))
				ostrm_putc(s, ch);
			else
				ostrm_printf(s, "\\x%02x", (unsigned)ch & 0xFFu);
		}
		ostrm_putc(s, '"');
		break;
	case CHEAX_EXT_FUNC:
		macro = (struct chx_ext_func *)val;
		if (macro->name == NULL)
			ostrm_printf(s, "[built-in function]");
		else
			ostrm_printf(s, "%s", macro->name);
		break;
	case CHEAX_USER_PTR:
		ostrm_printf(s, "%p", ((struct chx_user_ptr *)val)->value);
		break;
	case CHEAX_ENV:
		env = (struct chx_env *)val;
		while (env->is_bif) {
			if (env->value.bif[1] == NULL) {
				env = env->value.bif[0];
				continue;
			}

			ostrm_putc(s, '(');
			ostrm_show(c, s, &env->value.bif[1]->base);
			ostrm_printf(s, "\n");
			ostrm_show(c, s, &env->value.bif[0]->base);
			ostrm_putc(s, ')');
			return;
		}

		ostrm_printf(s, "((fn ()");

		struct rb_iter it;
		rb_iter_init(&it);
		for (struct full_sym *fs = rb_iter_first(&it, &env->value.norm.syms);
		     fs != NULL;
		     fs = rb_iter_next(&it))
		{
			ostrm_putc(s, '\n');
			ostrm_show_sym(c, s, fs);
		}
		ostrm_printf(s, "\n(env)))");
		break;
	}
}

static void
ostrm_show_as(CHEAX *c, struct ostrm *s, struct chx_value *val, int ty)
{
	if (cheax_is_basic_type(c, ty)) {
		ostrm_show_basic(c, s, val);
	} else if (cheax_is_user_type(c, ty)) {
		ostrm_printf(s, "(%s ", c->typestore.array[ty - CHEAX_TYPESTORE_BIAS].name);
		ostrm_show_as(c, s, val, cheax_get_base_type(c, ty));
		ostrm_printf(s, ")");
	} else {
		cheax_throwf(c, CHEAX_EEVAL, "ostrm_show_as(): unable to resolve type");
	}
}

void
ostrm_show(CHEAX *c, struct ostrm *s, struct chx_value *val)
{
	ostrm_show_as(c, s, val, cheax_type_of(val));
}

void
cheax_print(CHEAX *c, FILE *f, struct chx_value *val)
{
	struct fostrm fs;
	fostrm_init(&fs, f, c);
	ostrm_show(c, &fs.strm, val);
}
