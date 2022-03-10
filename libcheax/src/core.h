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

#ifndef CORE_H
#define CORE_H

#include "gc.h"
#include "sym.h"

enum {
	USABLE_BIT  = CHEAX_TYPE_MASK + 1,
	FIN_BIT     = USABLE_BIT,      /* has registered finalizer */
	NO_GC_BIT   = USABLE_BIT << 1, /* not GC allocated */
	GC_MARKED   = USABLE_BIT << 2, /* marked in use */
	GC_REFD     = USABLE_BIT << 3, /* carries cheax_ref() */
	NO_ESC_BIT  = USABLE_BIT << 4, /* presumed not to have escaped */
};

static inline bool
has_flag(int i, int f)
{
	return (i & f) == f;
}

static inline bool
has_fin_bit(struct chx_value *val)
{
	return has_flag(val->type, FIN_BIT);
}

static inline struct chx_value *
set_type(struct chx_value *value, int type)
{
	if (value != NULL)
		value->type = (value->type & ~CHEAX_TYPE_MASK) | (type & CHEAX_TYPE_MASK);
	return value;
}

struct type_cast {
	int to;
	chx_func_ptr cast;

	struct type_cast *next;
};

struct type_alias {
	const char *name;
	int base_type;

	chx_func_ptr print;

	struct type_cast *casts;
};

struct cheax {
	struct chx_env globals;
	struct chx_env *env;

	int stack_depth;

	int features;
	bool allow_redef;
	int mem_limit, stack_limit;

	int fhandle_type;

	struct {
		int state, code;
		struct chx_string *msg;
	} error;

	struct {
		const char **array;
		size_t len, cap;
	} user_error_names;

	struct {
		struct type_alias *array;
		size_t len, cap;
	} typestore;

	struct chx_sym **config_syms;

	struct gc_info gc;
};

/* v-to-i: value to int */
bool try_vtoi(struct chx_value *value, int *res);
/* v-to-d: value to double */
bool try_vtod(struct chx_value *value, double *res);

void cry(CHEAX *c, const char *name, int err, const char *frmt, ...);

void export_core_bltns(CHEAX *c);

#endif
