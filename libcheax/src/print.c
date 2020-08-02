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
#include <ctype.h>

#include "api.h"

void cheax_print(CHEAX *c, FILE *f, struct chx_value *val)
{
	switch (cheax_get_type(val)) {
	case CHEAX_NIL:
		fprintf(f, "()");
		break;
	case CHEAX_INT:
		fprintf(f, "%d", ((struct chx_int *)val)->value);
		break;
	case CHEAX_DOUBLE:
		fprintf(f, "%f", ((struct chx_double *)val)->value);
		break;
	case CHEAX_ID:
		fprintf(f, "%s", ((struct chx_id *)val)->id);
		break;
	case CHEAX_LIST:
		fprintf(f, "(");
		for (struct chx_list *list = (struct chx_list *)val; list; list = list->next) {
			cheax_print(c, f, list->value);
			if (list->next)
				fprintf(f, " ");
		}
		fprintf(f, ")");
		break;
	case CHEAX_QUOTE:
		fprintf(f, "'");
		cheax_print(c, f, ((struct chx_quote *)val)->value);
	case CHEAX_FUNC:
		fprintf(f, "(");
		struct chx_func *func = (struct chx_func *)val;
		if (func->eval_args)
			fprintf(f, "\\ ");
		else
			fprintf(f, "\\\\ ");
		cheax_print(c, f, func->args);
		struct chx_list *body = func->body;
		for (; body; body = body->next) {
			fprintf(f, "\n  ");
			cheax_print(c, f, body->value);
		}
		fprintf(f, ")");
		break;
	case CHEAX_STRING:
		fputc('"', f);
		struct chx_string *string = (struct chx_string *)val;
		for (int i = 0; i < string->len; ++i) {
			char ch = string->value[i];
			if (ch == '"')
				fputs("\\\"", f);
			else if (isprint(ch))
				fputc(ch, f);
			else
				fprintf(f, "\\x%02x", (int)ch);
		}
		fputc('"', f);
		break;
	case CHEAX_EXT_FUNC:
		; struct chx_ext_func *macro = (struct chx_ext_func *)val;
		if (macro->name == NULL)
			fputs("[built-in function]", f);
		else
			fputs(macro->name, f);
		break;
	default:
		fprintf(f, "%p", ((struct chx_user_ptr *)val)->value);
		break;
	}
}
