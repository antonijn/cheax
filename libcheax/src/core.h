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

/* for rtflags field in chx_value */
enum {
	GC_BIT      = 0x0001, /* allocated by gc */
	FIN_BIT     = 0x0002, /* has registered finalizer */
	GC_MARKED   = 0x0004, /* marked in use by gc (temporary) */
	REF_BIT     = 0x0008, /* carries cheax_ref() */
	NO_ESC_BIT  = 0x0010, /* chx_env presumed not to have escaped */
	DEBUG_LIST  = 0x0020, /* list is struct debug_list */
};

static inline bool
has_flag(int i, int f)
{
	return (i & f) == f;
}

struct chx_string {
	struct chx_value base;
	char *value;
	size_t len;
	struct chx_string *orig;
};

struct debug_info {
	const char *file;
	int pos, line;
};

struct debug_list {
	struct chx_list base;
	struct debug_info info;
};

struct debug_list *debug_list(CHEAX *c,
                              struct chx_value *car,
                              struct chx_list *cdr,
                              struct debug_info info);

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
	bool allow_redef, gen_debug_info;
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

struct chx_int *typecode(CHEAX *c, int value);
struct chx_int *errorcode(CHEAX *c, int value);

void export_core_bltns(CHEAX *c);

#endif
