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

#include "rbtree.h"

enum {
	FIN_BIT       = CHEAX_TYPE_MASK + 1,
	BIF_ENV_BIT   = FIN_BIT << 1,
	NO_GC_BIT     = FIN_BIT << 2,
	GC_NOT_IN_USE = FIN_BIT << 3,
	GC_IN_USE     = 0,
	GC_BITS       = NO_GC_BIT | GC_NOT_IN_USE,
};

static inline int
gc_bits(struct chx_value *val)
{
	return val->type & GC_BITS;
}

static inline void
set_gc_bits(struct chx_value *val, int bits)
{
	val->type = (val->type & ~GC_BITS) | (bits & GC_BITS);
}

static inline bool
has_fin_bit(struct chx_value *val)
{
	return (val->type & FIN_BIT) != 0;
}

static inline bool
has_bif_env_bit(struct chx_value *val)
{
	return (val->type & BIF_ENV_BIT) != 0;
}

enum {
	CTYPE_NONE, /* only for non-synchronized variables */
	CTYPE_INT,
	CTYPE_FLOAT,
	CTYPE_DOUBLE,
};

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

	int max_stack_depth, stack_depth;
	int fhandle_type;

	struct {
		int state;
		int code;
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

#ifndef USE_BOEHM_GC
	struct {
		struct rb_tree all_objects;
		size_t all_mem, prev_run;
	} gc;
#endif
};

static inline int
set_type(struct chx_value *value, int type)
{
	value->type = (value->type & ~CHEAX_TYPE_MASK) | (type & CHEAX_TYPE_MASK);
	return type;
}

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
