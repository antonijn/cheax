/* Copyright (c) 2016, Antonie Blom
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

struct ext_var {
	enum cheax_type ty;
	void *var;
};
enum varflags {
	SF_DEFAULT = 0x00,
	SF_SYNCED = 0x01,
	SF_RO = 0x02,
	SF_NODUMP = 0x04
};
struct variable {
	const char *name;
	unsigned hash;
	enum varflags flags;
	struct chx_value *value;
	struct ext_var sync_var;
	struct variable *below;
};

struct cheax {
	struct variable *locals_top;
	int max_stack_depth, stack_depth;
	enum chx_error error;
};

struct chx_cons *cheax_cons(struct chx_value *car, struct chx_cons *cdr);
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
