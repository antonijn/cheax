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

#ifndef API_H
#define API_H

#include <cheax.h>

#include "rbtree.h"

enum {
	CTYPE_NONE, /* only for non-synchronized variables */
	CTYPE_INT,
	CTYPE_FLOAT,
	CTYPE_DOUBLE,
};

struct variable {
	const char *name;
	enum chx_varflags flags;
	int ctype;

	union {
		struct chx_value *norm;

		int *sync_int;
		float *sync_float;
		double *sync_double;
	} value;

	struct variable *below;
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

struct cheax {
	struct variable *locals_top;

	int max_stack_depth, stack_depth;
	int fhandle_type;

	struct {
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

bool try_convert_to_int(struct chx_value *value, int *res);
bool try_convert_to_double(struct chx_value *value, double *res);

struct variable *find_sym(CHEAX *c, const char *name);
struct variable *def_sym(CHEAX *c, const char *name, enum chx_varflags flags);
void cry(CHEAX *c, const char *name, int err, const char *frmt, ...);

/* defined in builtins.c */
void export_builtins(CHEAX *c);

#endif
