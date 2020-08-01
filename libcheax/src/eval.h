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

#ifndef EVAL_H
#define EVAL_H

#include <cheax.h>
#include <stdarg.h>

enum {
	CTYPE_NONE, /* only for non-synchronized variables */
	CTYPE_INT,
	CTYPE_FLOAT,
	CTYPE_DOUBLE,

	CTYPE_USER_TYPE = CHEAX_USER_TYPE,
};

struct variable {
	const char *name;
	enum cheax_varflags flags;
	int ctype;

	union {
		struct chx_value *norm;

		int *sync_int;
		float *sync_float;
		double *sync_double;
	} value;

	struct variable *below;
};

struct cheax {
	struct variable *locals_top;
	int max_stack_depth, stack_depth;
	int user_type_count;
	int fhandle_type;
	enum chx_error error;
};

struct chx_list *cheax_cons(struct chx_value *car, struct chx_list *cdr);
bool pan_match(CHEAX *c, struct chx_value *pan, struct chx_value *match);
struct variable *find_sym(CHEAX *c, const char *name);

static inline void cry(CHEAX *c, const char *name, const char *frmt, ...)
{
	va_list ap;
	va_start(ap, frmt);
#ifndef WIN32
	fprintf(stderr, "\033[31m", name);
#endif
	fprintf(stderr, "(%s): ", name);
	vfprintf(stderr, frmt, ap);
#ifndef WIN32
	fprintf(stderr, "\033[0m", name);
#endif
	fputc('\n', stderr);
}

#endif
