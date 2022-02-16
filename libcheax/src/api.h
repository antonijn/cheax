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
	NO_GC_BIT   = CHEAX_TYPE_MASK + 1,
	FIN_BIT     = NO_GC_BIT << 1,
	BIF_ENV_BIT = NO_GC_BIT << 2,
};

static inline bool
has_no_gc_bit(struct chx_value *val)
{
	return (val->type & NO_GC_BIT) != 0;
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

struct variable {
	const char *name;
	int flags;
	int ctype;

	union {
		struct chx_value *norm;

		int *sync_int;
		float *sync_float;
		double *sync_double;
	} value;
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
	value->type = (value->type & ~CHEAX_TYPE_MASK) | type;
	return type;
}

bool try_convert_to_int(struct chx_value *value, int *res);
bool try_convert_to_double(struct chx_value *value, double *res);

struct variable *find_sym_in(struct chx_env *env, const char *name);
struct variable *find_sym(CHEAX *c, const char *name);
void cry(CHEAX *c, const char *name, int err, const char *frmt, ...);

/* defined in builtins.c */
void export_builtins(CHEAX *c);

#endif
