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
ostrm_show_basic(CHEAX *c, struct ostrm *s, struct chx_value val)
{
	struct chx_env *env;
	struct chx_form *macro;

	int ty = cheax_resolve_type(c, val.type);
	switch (ty) {
	case CHEAX_INT:
		ostrm_printi(s, val.data.as_int, 0, 0, 'd');
		break;
	case CHEAX_DOUBLE:
		ostrm_printf(s, "%f", val.data.as_double);
		break;
	case CHEAX_BOOL:
		ostrm_printf(s, "%s", val.data.as_int ? "true" : "false");
		break;
	case CHEAX_ID:
		ostrm_printf(s, "%s", val.data.as_id->value);
		break;
	case CHEAX_LIST:
		ostrm_putc(s, '(');
		for (struct chx_list *list = val.data.as_list; list != NULL; list = list->next) {
			ostrm_show(c, s, list->value);
			if (list->next)
				ostrm_putc(s, ' ');
		}
		ostrm_putc(s, ')');
		break;
	case CHEAX_QUOTE:
		ostrm_putc(s, '\'');
		ostrm_show(c, s, val.data.as_quote->value);
		break;
	case CHEAX_BACKQUOTE:
		ostrm_putc(s, '`');
		ostrm_show(c, s, val.data.as_quote->value);
		break;
	case CHEAX_COMMA:
		ostrm_putc(s, ',');
		ostrm_show(c, s, val.data.as_quote->value);
		break;
	case CHEAX_SPLICE:
		ostrm_printf(s, ",@");
		ostrm_show(c, s, val.data.as_quote->value);
		break;
	case CHEAX_FUNC:
		ostrm_putc(s, '(');
		struct chx_func *func = val.data.as_func;
		ostrm_printf(s, "fn ");
		ostrm_show(c, s, func->args);
		for (struct chx_list *body = func->body; body != NULL; body = body->next) {
			ostrm_printf(s, "\n  ");
			ostrm_show(c, s, body->value);
		}
		ostrm_putc(s, ')');
		break;
	case CHEAX_STRING:
		ostrm_putc(s, '"');
		struct chx_string *string = val.data.as_string;
		for (size_t i = 0; i < string->len; ++i) {
			char ch = string->value[i];
			if (ch == '"' || ch == '\\')
				ostrm_printf(s, "\\%c", ch);
			else if (c_isgraph(ch) || ch == ' ')
				ostrm_putc(s, ch);
			else
				ostrm_printf(s, "\\x%02X", (unsigned)ch & 0xFFu);
		}
		ostrm_putc(s, '"');
		break;
	case CHEAX_EXT_FUNC:
	case CHEAX_SPECIAL_FORM:
	case CHEAX_SPECIAL_TAIL_FORM:
		macro = val.data.as_form;
		if (macro->name == NULL)
			ostrm_printf(s, "[native form]");
		else
			ostrm_printf(s, "%s", macro->name);
		break;
	case CHEAX_USER_PTR:
		ostrm_printf(s, "%p", val.data.user_ptr);
		break;
	case CHEAX_ENV:
		env = val.data.as_env;
		while (env->is_bif) {
			if (env->value.bif[1] == NULL) {
				env = env->value.bif[0];
				continue;
			}

			ostrm_putc(s, '(');
			ostrm_show(c, s, cheax_env_value(env->value.bif[1]));
			ostrm_printf(s, "\n");
			ostrm_show(c, s, cheax_env_value(env->value.bif[0]));
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
ostrm_show_as(CHEAX *c, struct ostrm *s, struct chx_value val, int ty)
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
ostrm_show(CHEAX *c, struct ostrm *s, struct chx_value val)
{
	ostrm_show_as(c, s, val, val.type);
}

void
cheax_print(CHEAX *c, FILE *f, struct chx_value val)
{
	struct fostrm fs;
	fostrm_init(&fs, f, c);
	ostrm_show(c, &fs.strm, val);
}
