/* Copyright (c) 2024, Antonie Blom
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
show_sym(struct htab_entry *item, void *info)
{
	struct ostrm *s = info;
	struct full_sym *fs = container_of(item, struct full_sym, entry);
	struct chx_sym *sym = &fs->sym;
	if (sym->get == NULL)
		cheax_ostrm_printf_(s, "\n;%s", fs->name);
	else
		cheax_ostrm_printf_(s, "\n(%s %s)", (sym->set == NULL) ? "def" : "var", fs->name);
}

static void
cheax_ostrm_show_basic_(CHEAX *c, struct ostrm *s, struct chx_value val)
{
	struct chx_env *env;
	struct chx_ext_func *macro;
	struct chx_special_op *specop;

	int ty = cheax_resolve_type(c, val.type);
	switch (ty) {
	case CHEAX_INT:
		cheax_ostrm_printi_(s, val.data.as_int, 0, 0, 'd');
		break;
	case CHEAX_DOUBLE:
		cheax_ostrm_printf_(s, "%f", val.data.as_double);
		break;
	case CHEAX_BOOL:
		cheax_ostrm_printf_(s, "%s", val.data.as_int ? "true" : "false");
		break;
	case CHEAX_ID:
		cheax_ostrm_printf_(s, "%s", val.data.as_id->value);
		break;
	case CHEAX_LIST:
		cheax_ostrm_putc_(s, '(');
		for (struct chx_list *list = val.data.as_list; list != NULL; list = list->next) {
			cheax_ostrm_show_(c, s, list->value);
			if (list->next)
				cheax_ostrm_putc_(s, ' ');
		}
		cheax_ostrm_putc_(s, ')');
		break;
	case CHEAX_QUOTE:
		cheax_ostrm_putc_(s, '\'');
		cheax_ostrm_show_(c, s, val.data.as_quote->value);
		break;
	case CHEAX_BACKQUOTE:
		cheax_ostrm_putc_(s, '`');
		cheax_ostrm_show_(c, s, val.data.as_quote->value);
		break;
	case CHEAX_COMMA:
		cheax_ostrm_putc_(s, ',');
		cheax_ostrm_show_(c, s, val.data.as_quote->value);
		break;
	case CHEAX_SPLICE:
		cheax_ostrm_printf_(s, ",@");
		cheax_ostrm_show_(c, s, val.data.as_quote->value);
		break;
	case CHEAX_FUNC:
		cheax_ostrm_putc_(s, '(');
		struct chx_func *func = val.data.as_func;
		cheax_ostrm_printf_(s, "fn ");
		cheax_ostrm_show_(c, s, func->args);
		for (struct chx_list *body = func->body; body != NULL; body = body->next) {
			cheax_ostrm_printf_(s, "\n  ");
			cheax_ostrm_show_(c, s, body->value);
		}
		cheax_ostrm_putc_(s, ')');
		break;
	case CHEAX_STRING:
		cheax_ostrm_putc_(s, '"');
		struct chx_string *string = val.data.as_string;
		for (size_t i = 0; i < string->len; ++i) {
			char ch = string->value[i];
			if (ch == '"' || ch == '\\')
				cheax_ostrm_printf_(s, "\\%c", ch);
			else if (cheax_isgraph_(ch) || ch == ' ')
				cheax_ostrm_putc_(s, ch);
			else
				cheax_ostrm_printf_(s, "\\x%02X", (unsigned)ch & 0xFFu);
		}
		cheax_ostrm_putc_(s, '"');
		break;
	case CHEAX_EXT_FUNC:
		macro = val.data.as_ext_func;
		if (macro->name == NULL)
			cheax_ostrm_printf_(s, "[external function]");
		else
			cheax_ostrm_printf_(s, "%s", macro->name);
		break;
	case CHEAX_SPECIAL_OP:
		specop = val.data.as_special_op;
		if (specop->name == NULL)
			cheax_ostrm_printf_(s, "[special operator]");
		else
			cheax_ostrm_printf_(s, "%s", specop->name);
		break;
	case CHEAX_USER_PTR:
		cheax_ostrm_printf_(s, "%p", val.data.user_ptr);
		break;
	case CHEAX_ENV:
		env = val.data.as_env;
		while (env->is_bif) {
			if (env->value.bif[1] == NULL) {
				env = env->value.bif[0];
				continue;
			}

			cheax_ostrm_putc_(s, '(');
			cheax_ostrm_show_(c, s, cheax_env_value(env->value.bif[1]));
			cheax_ostrm_printf_(s, "\n");
			cheax_ostrm_show_(c, s, cheax_env_value(env->value.bif[0]));
			cheax_ostrm_putc_(s, ')');
			return;
		}

		cheax_ostrm_printf_(s, "((fn ()");
		cheax_htab_foreach_(&env->value.norm.syms, show_sym, s);
		cheax_ostrm_printf_(s, "\n(env)))");
		break;
	}
}

static void
cheax_ostrm_show_as_(CHEAX *c, struct ostrm *s, struct chx_value val, int ty)
{
	if (cheax_is_basic_type(c, ty)) {
		cheax_ostrm_show_basic_(c, s, val);
	} else if (cheax_is_user_type(c, ty)) {
		cheax_ostrm_printf_(s, "(%s ", c->typestore.array[ty - CHEAX_TYPESTORE_BIAS].name);
		cheax_ostrm_show_as_(c, s, val, cheax_get_base_type(c, ty));
		cheax_ostrm_printf_(s, ")");
	} else {
		cheax_throwf(c, CHEAX_EEVAL, "cheax_ostrm_show_as_(): unable to resolve type");
	}
}

void
cheax_ostrm_show_(CHEAX *c, struct ostrm *s, struct chx_value val)
{
	cheax_ostrm_show_as_(c, s, val, val.type);
}

void
cheax_print(CHEAX *c, FILE *f, struct chx_value val)
{
	struct fostrm fs;
	cheax_fostrm_init_(&fs, f, c);
	cheax_ostrm_show_(c, &fs.strm, val);
}
