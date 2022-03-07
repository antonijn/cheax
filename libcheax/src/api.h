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

#ifndef API_H
#define API_H

#include <cheax.h>

#include "gc.h"
#include "rbtree.h"

enum {
	USABLE_BIT  = CHEAX_TYPE_MASK + 1,
	FIN_BIT     = USABLE_BIT,          /* has registered finalizer */
	NO_GC_BIT   = USABLE_BIT << 1,     /* not GC allocated */
	GC_MARKED   = USABLE_BIT << 2,     /* marked in use */
	GC_REFD     = USABLE_BIT << 3,     /* carries cheax_ref() */
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
	value->type = (value->type & ~CHEAX_TYPE_MASK) | (type & CHEAX_TYPE_MASK);
	return value;
}

struct full_sym {
	const char *name;
	struct chx_sym sym;
};

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

struct chx_env {
	struct chx_value base;
	bool is_bif;
	union {
		struct chx_env *bif[2];

		struct {
			struct rb_tree syms;
			struct chx_env *below;
		} norm;
	} value;
};

struct cheax {
	struct chx_env globals;
	struct chx_env *env;

	int features;

	int max_stack_depth, stack_depth;
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

	struct gc_info gc;
};

/* v-to-i: value to int */
bool try_vtoi(struct chx_value *value, int *res);
/* v-to-d: value to double */
bool try_vtod(struct chx_value *value, double *res);

struct chx_sym *find_sym_in(struct chx_env *env, const char *name);
struct chx_sym *find_sym(CHEAX *c, const char *name);
void cry(CHEAX *c, const char *name, int err, const char *frmt, ...);

/* defined in builtins.c */
void export_builtins(CHEAX *c);

#endif
