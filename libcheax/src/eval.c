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
#include "eval.h"
#include "builtins.h"

#include <gc.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static struct variable *def_sym(CHEAX *c, const char *name, enum cheax_varflags flags);

struct chx_int *cheax_int(int value)
{
	struct chx_int *res = GC_MALLOC(sizeof(struct chx_int));
	res->base.type = CHEAX_INT;
	res->value = value;
	return res;
}
struct chx_double *cheax_double(double value)
{
	struct chx_double *res = GC_MALLOC(sizeof(struct chx_double));
	res->base.type = CHEAX_DOUBLE;
	res->value = value;
	return res;
}
struct chx_list *cheax_list(struct chx_value *car, struct chx_list *cdr)
{
	struct chx_list *res = GC_MALLOC(sizeof(struct chx_list));
	res->base.type = CHEAX_LIST;
	res->value = car;
	res->next = cdr;
	return res;
}

enum chx_error cheax_errno(CHEAX *c)
{
	enum chx_error res = c->error;
	c->error = 0;
	return res;
}

static bool pan_match_cheax_list(CHEAX *c, struct chx_list *pan, struct chx_list *match);

bool pan_match(CHEAX *c, struct chx_value *pan, struct chx_value *match)
{
	if (pan == NULL)
		return match == NULL;
	if (pan->type == CHEAX_ID) {
		def_sym(c, ((struct chx_id *)pan)->id, 0)->value.norm = match;
		return true;
	}
	if (match == NULL)
		return false;
	if (pan->type == CHEAX_INT) {
		if (match->type != CHEAX_INT)
			return false;
		return ((struct chx_int *)pan)->value == ((struct chx_int *)match)->value;
	}
	if (pan->type == CHEAX_DOUBLE) {
		if (match->type != CHEAX_DOUBLE)
			return false;
		return ((struct chx_double *)pan)->value == ((struct chx_double *)match)->value;
	}
	if (pan->type == CHEAX_LIST) {
		struct chx_list *pan_list = (struct chx_list *)pan;
		if (match->type != CHEAX_LIST)
			return false;
		struct chx_list *match_list = (struct chx_list *)match;

		return pan_match_cheax_list(c, pan_list, match_list);
	}
	return false;
}

static bool pan_match_colon_cheax_list(CHEAX *c, struct chx_list *pan, struct chx_list *match)
{
	if (!pan->next)
		return pan_match(c, pan->value, &match->base);
	if (!match)
		return false;
	if (!pan_match(c, pan->value, match->value))
		return false;
	return pan_match_colon_cheax_list(c, pan->next, match->next);
}

static bool pan_match_cheax_list(CHEAX *c, struct chx_list *pan, struct chx_list *match)
{
	if (cheax_get_type(pan->value) == CHEAX_ID
	 && !strcmp((((struct chx_id *)pan->value)->id), ":"))
	{
		return pan_match_colon_cheax_list(c, pan->next, match);
	}

	while (pan && match) {
		if (!pan_match(c, pan->value, match->value))
			return false;

		pan = pan->next;
		match = match->next;
	}

	return (pan == NULL) && (match == NULL);
}

struct variable *find_sym(CHEAX *c, const char *name)
{
	for (struct variable *ht = c->locals_top; ht; ht = ht->below)
		if (!strcmp(name, ht->name))
			return ht;
	return NULL;
}

static struct variable *def_sym(CHEAX *c, const char *name, enum cheax_varflags flags)
{
	struct variable *new = GC_MALLOC(sizeof(struct variable));
	new->flags = flags;
	new->ctype = CTYPE_NONE;
	new->value.norm = NULL;
	new->name = name;
	new->below = c->locals_top;
	c->locals_top = new;
	return new;
}

CHEAX *cheax_init(void)
{
	CHEAX *res = malloc(sizeof(struct cheax));
	res->locals_top = NULL;
	res->max_stack_depth = 0x1000;
	res->stack_depth = 0;
	res->user_type_count = 0;
	export_builtins(res);
	return res;
}
void cheax_destroy(CHEAX *c)
{
	free(c);
}

int cheax_get_max_stack_depth(CHEAX *c)
{
	return c->max_stack_depth;
}
void cheax_set_max_stack_depth(CHEAX *c, int max_stack_depth)
{
	if (max_stack_depth > 0)
		c->max_stack_depth = max_stack_depth;
	else
		cry(c, "cheax_set_max_stack_depth", "Maximum stack depth must be positive");
}

int cheax_get_type(struct chx_value *v)
{
	if (v == NULL)
		return CHEAX_NIL;

	return v->type;
}
int cheax_new_user_type(CHEAX *c)
{
	return CHEAX_USER_TYPE + c->user_type_count++;
}
int cheax_is_user_type(int type)
{
	return type >= CHEAX_USER_TYPE;
}

void cheax_sync_int(CHEAX *c, const char *name, int *var, enum cheax_varflags flags)
{
	struct variable *newsym = def_sym(c, name, flags | CHEAX_SYNCED);
	newsym->ctype = CTYPE_INT;
	newsym->value.sync_int = var;
}
void cheax_sync_float(CHEAX *c, const char *name, float *var, enum cheax_varflags flags)
{
	struct variable *newsym = def_sym(c, name, flags | CHEAX_SYNCED);
	newsym->ctype = CTYPE_FLOAT;
	newsym->value.sync_float = var;
}
void cheax_sync_double(CHEAX *c, const char *name, double *var, enum cheax_varflags flags)
{
	struct variable *newsym = def_sym(c, name, flags | CHEAX_SYNCED);
	newsym->ctype = CTYPE_DOUBLE;
	newsym->value.sync_double = var;
}

void cheax_defmacro(CHEAX *c, const char *name, chx_funcptr fun)
{
	struct chx_ext_func *extf = GC_MALLOC(sizeof(struct chx_ext_func));
	extf->base.type = CHEAX_EXT_FUNC;
	extf->perform = fun;
	extf->name = name;
	struct variable *v = def_sym(c, name, CHEAX_READONLY);
	v->value.norm = &extf->base;
}
void cheax_decl_user_data(CHEAX *c, const char *name, void *ptr, int user_type)
{
	if (!cheax_is_user_type(user_type) || user_type >= CHEAX_USER_TYPE + c->user_type_count) {
		cry(c, "cheax_decl_user_data", "Requires user type");
		c->error = CHEAX_EAPI;
		return;
	}

	struct chx_ptr *res = GC_MALLOC(sizeof(struct chx_ptr));
	res->base.type = user_type;
	res->ptr = ptr;
	struct variable *v = def_sym(c, name, CHEAX_READONLY);
	v->value.norm = &res->base;
}

int cheax_load_prelude(CHEAX *c)
{
	FILE *f = fopen(CMAKE_INSTALL_PREFIX "/share/cheax/prelude.chx", "rb");
	if (!f)
		return -1;
	cheax_exec(c, f);
	fclose(f);
	return 0;
}

void cheax_exec(CHEAX *c, FILE *f)
{
	char shebang[2] = { 0 };
	fread(shebang, 2, 1, f);
	if (shebang[0] == '#' && shebang[1] == '!') {
		while (fgetc(f) != '\n')
			;
	} else {
		ungetc(shebang[1], f);
		ungetc(shebang[0], f);
	}

	struct chx_value *v;
	while (v = cheax_read(c, f))
		cheax_eval(c, v);
}

static struct chx_value *cheax_eval_sexpr(CHEAX *c, struct chx_list *input);

struct chx_value *cheax_eval(CHEAX *c, struct chx_value *input)
{
	switch (cheax_get_type(input)) {
	case CHEAX_ID:
		; struct variable *sym = find_sym(c, ((struct chx_id *)input)->id);
		if (!sym) {
			cry(c, "eval", "No such symbol '%s'", ((struct chx_id *)input)->id);
			c->error = CHEAX_ENOSYM;
			return NULL;
		}
		if ((sym->flags & CHEAX_SYNCED) == 0)
			return sym->value.norm;

		switch (sym->ctype) {
		case CTYPE_INT:
			; struct chx_int *int_res = GC_MALLOC(sizeof(struct chx_int));
			*int_res = (struct chx_int){ { CHEAX_INT }, *sym->value.sync_int };
			return &int_res->base;
		case CTYPE_DOUBLE:
			; struct chx_double *double_res = GC_MALLOC(sizeof(struct chx_double));
			*double_res = (struct chx_double){ { CHEAX_DOUBLE }, *sym->value.sync_double };
			return &double_res->base;
		case CTYPE_FLOAT:
			; struct chx_double *float_res = GC_MALLOC(sizeof(struct chx_double));
			*float_res = (struct chx_double){ { CHEAX_DOUBLE }, *sym->value.sync_float };
			return &float_res->base;
		}
	case CHEAX_LIST:
		if (c->stack_depth >= c->max_stack_depth) {
			cry(c, "eval", "Stack overflow! (maximum stack depth = %d)", c->max_stack_depth);
			c->error = CHEAX_ESTACK;
			return NULL;
		}

		int prev_stack_depth = c->stack_depth++;
		struct chx_value *res = cheax_eval_sexpr(c, (struct chx_list *)input);
		c->stack_depth = prev_stack_depth;

		return res;
	case CHEAX_QUOTE:
		return ((struct chx_quote *)input)->value;

	default:
		return input;
	}
}

static struct chx_value *expand_macro(struct variable *args_top,
                                      struct variable *locals_top,
                                      struct chx_value *val)
{
	struct chx_id *as_id = (struct chx_id *)val;
	struct chx_list *as_cons = (struct chx_list *)val;
	struct chx_quote *as_quote = (struct chx_quote *)val;
	switch (cheax_get_type(val)) {
	case CHEAX_ID:
		for (struct variable *v = args_top; v != locals_top; v = v->below)
			if (!strcmp(v->name, as_id->id))
				return v->value.norm; /* args cannot be synced afaik */
		return val;
	case CHEAX_LIST:
		; struct chx_list *cons_res = NULL;
		struct chx_list **cons_last = &cons_res;
		for (; as_cons; as_cons = as_cons->next) {
			*cons_last = cheax_list(expand_macro(args_top, locals_top, as_cons->value), NULL);
			cons_last = &(*cons_last)->next;
		}
		return &cons_res->base;
	case CHEAX_QUOTE:
		; struct chx_quote *quotres = GC_MALLOC(sizeof(struct chx_quote));
		quotres->base.type = CHEAX_QUOTE;
		quotres->value = expand_macro(args_top, locals_top, as_quote->value);
		return &quotres->base;
	default:
		return val;
	}
}
static struct chx_value *call_macro(CHEAX *c, struct chx_func *lda, struct chx_list *args)
{
	struct variable *prev_top = c->locals_top;
	if (!pan_match(c, lda->args, &args->base)) {
		cry(c, "eval", "Invalid (number of) arguments");
		c->error = CHEAX_EMATCH;
		return NULL;
	}
	struct variable *new_top = c->locals_top;
	c->locals_top = prev_top;

	struct chx_value *retval = NULL;
	for (struct chx_list *cons = lda->body; cons; cons = cons->next)
		retval = cheax_eval(c, expand_macro(new_top, prev_top, cons->value));

	return retval;
}
static struct chx_value *call_func(CHEAX *c, struct chx_func *lda, struct chx_list *args)
{
	if (!pan_match(c, lda->args, &args->base)) {
		cry(c, "eval", "Invalid (number of) arguments");
		c->error = CHEAX_EMATCH;
		return NULL;
	}

	struct chx_value *retval = NULL;
	for (struct chx_list *cons = lda->body; cons; cons = cons->next)
		retval = cheax_eval(c, cons->value);
	return retval;
}

static struct chx_value *cheax_eval_sexpr(CHEAX *c, struct chx_list *input)
{
	struct chx_value *func = cheax_eval(c, input->value);
	switch (cheax_get_type(func)) {
	case CHEAX_NIL:
		cry(c, "eval", "Cannot call nil");
		c->error = CHEAX_ENIL;
		return NULL;
	case CHEAX_EXT_FUNC:
		; struct chx_ext_func *extf = (struct chx_ext_func *)func;
		return extf->perform(c, input->next);
	case CHEAX_FUNC:
		; struct chx_func *lda = (struct chx_func *)func;
		if (!lda->eval_args) {
			/* macro call */
			struct chx_list *args = input->next;
			return call_macro(c, lda, args);
		}

		/* function call */

		/* evaluate arguments */
		struct chx_list *args = NULL;
		struct chx_list **args_last = &args;
		for (struct chx_list *arg = input->next; arg; arg = arg->next) {
			*args_last = cheax_list(cheax_eval(c, arg->value), NULL);
			args_last = &(*args_last)->next;
		}

		/* create new context for function to run in */
		struct variable *prev_locals_top = c->locals_top;
		c->locals_top = lda->locals_top;

		struct chx_value *retval = call_func(c, lda, args);
		/* restore previous context */
		c->locals_top = prev_locals_top;
		return retval;
	}

	cry(c, "eval", "Invalid function call");
	c->error = CHEAX_EEVAL;
	return NULL;
}

void cheax_print(FILE *c, struct chx_value *val)
{
	switch (cheax_get_type(val)) {
	case CHEAX_NIL:
		fprintf(c, "()");
		break;
	case CHEAX_INT:
		fprintf(c, "%d", ((struct chx_int *)val)->value);
		break;
	case CHEAX_DOUBLE:
		fprintf(c, "%f", ((struct chx_double *)val)->value);
		break;
	case CHEAX_ID:
		fprintf(c, "%s", ((struct chx_id *)val)->id);
		break;
	case CHEAX_LIST:
		fprintf(c, "(");
		for (struct chx_list *list = (struct chx_list *)val; list; list = list->next) {
			cheax_print(c, list->value);
			if (list->next)
				fprintf(c, " ");
		}
		fprintf(c, ")");
		break;
	case CHEAX_QUOTE:
		fprintf(c, "'");
		cheax_print(c, ((struct chx_quote *)val)->value);
	case CHEAX_FUNC:
		fprintf(c, "(");
		struct chx_func *func = (struct chx_func *)val;
		if (func->eval_args)
			fprintf(c, "\\ ");
		else
			fprintf(c, "\\\\ ");
		cheax_print(c, func->args);
		struct chx_list *body = func->body;
		for (; body; body = body->next) {
			fprintf(c, "\n  ");
			cheax_print(c, body->value);
		}
		fprintf(c, ")");
		break;
	case CHEAX_STRING:
		fputc('"', c);
		struct chx_string *string = (struct chx_string *)val;
		for (int i = 0; i < string->len; ++i) {
			char ch = string->value[i];
			if (ch == '"')
				fputs("\\\"", c);
			else if (isprint(ch))
				fputc(ch, c);
			else
				fprintf(c, "\\x%02x", (int)ch);
		}
		fputc('"', c);
		break;
	case CHEAX_EXT_FUNC:
		; struct chx_ext_func *macro = (struct chx_ext_func *)val;
		if (macro->name == NULL)
			fputs("[built-in function]", c);
		else
			fputs(macro->name, c);
		break;
	default:
		fprintf(c, "%p", ((struct chx_ptr *)val)->ptr);
		break;
	}
}
