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

#ifndef CORE_H
#define CORE_H

#include "gc.h"
#include "types.h"
#include "sym.h"

#define ASSERT_NOT_NULL(name, x, ret) do { \
	if ((x) == NULL) {                 \
		cheax_throwf(c, CHEAX_EAPI, name "(): `" #x "' cannot be NULL"); \
		return (ret);              \
	}                                  \
} while (false)

#define ASSERT_NOT_NULL_VOID(name, x) do { \
	if ((x) == NULL) {                 \
		cheax_throwf(c, CHEAX_EAPI, name "(): `" #x "' cannot be NULL"); \
		return;                    \
	}                                  \
} while (false)

/* for rtflags field in chx_value */
enum {
	GC_BIT      = 0x0001, /* allocated by gc */
	FIN_BIT     = 0x0002, /* has registered finalizer */
	GC_MARKED   = 0x0004, /* marked in use by gc (temporary) */
	REF_BIT     = 0x0008, /* carries cheax_ref() */
	NO_ESC_BIT  = 0x0010, /* chx_env presumed not to have escaped */
	LOC_INFO    = 0x0020, /* list is struct loc_debug_list */
	ORIG_INFO   = 0x0040, /* list is struct orig_debug_list */
	DEBUG_BITS  = 0x0060,
	PREPROC_BIT = 0x0080, /* This form has been preprocessed */
};

static inline bool
has_flag(int i, int f)
{
	return (i & f) == f;
}

struct loc_debug_info {
	const char *file;
	int pos, line;
};

struct chx_list *loc_debug_list(CHEAX *c,
                                struct chx_value car,
                                struct chx_list *cdr,
                                struct loc_debug_info info);
struct chx_list *orig_debug_list(CHEAX *c,
                                 struct chx_value car,
                                 struct chx_list *cdr,
                                 struct chx_list *orig_form);

struct loc_debug_info *get_loc_debug_info(struct chx_list *list);
struct chx_list *get_orig_form(struct chx_list *list);

struct type_cast {
	int to;
	chx_func_ptr cast;

	struct type_cast *next;
};

struct type_alias {
	char *name;
	int base_type;

	chx_func_ptr print;

	struct type_cast *casts;
};

struct cheax {
	/* contains all global symbols defined at runtime */
	struct chx_env global_ns;
	/* contains all special operators (not directly accessible) */
	struct chx_env specop_ns;
	/* contains all macros */
	struct chx_env macro_ns;

	/* current environment */
	struct chx_env *env;

	/*
	 * either a reference to global_env_struct, or NULL when running
	 * in macro expansion mode
	 */
	struct chx_env *global_env;

	int stack_depth;

	/* see config.c for explanation of these fields */
	int features;
	bool allow_redef, gen_debug_info, tail_call_elimination, hyper_gc;
	int mem_limit, stack_limit;

	/* file handle type code */
	int fhandle_type;

	struct {
		int code;
		struct chx_string *msg;
	} error;

	struct {
		struct bt_entry {
			struct loc_debug_info info;
			char line1[81];
			char line2[81];
		} *array;
		size_t len, limit;
		bool truncated;
		struct chx_list *last_call;
	} bt; /* backtrace */

	struct {
		char **array;
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
bool try_vtoi(struct chx_value value, chx_int *res);
/* v-to-d: value to double */
bool try_vtod(struct chx_value value, chx_double *res);
double vtod(struct chx_value value);

#define typecode(X)  ((struct chx_value){ .type = CHEAX_TYPECODE,  .data.as_int = (X) })
#define errorcode(X) ((struct chx_value){ .type = CHEAX_ERRORCODE, .data.as_int = (X) })

void export_core_bltns(CHEAX *c);

#endif
