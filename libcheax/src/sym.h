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

#ifndef SYM_H
#define SYM_H

#include <cheax.h>

#include "rbtree.h"

struct full_sym {
	const char *name;
	bool allow_redef;
	struct chx_sym sym;
};

struct chx_env {
	unsigned rtflags;
	bool is_bif;
	union {
		struct chx_env *bif[2];

		struct {
			struct rb_tree syms;
			struct chx_env *below;
		} norm;
	} value;
};

struct chx_env *norm_env_init(CHEAX *c, struct chx_env *env, struct chx_env *below);
void norm_env_cleanup(struct chx_env *env);

void export_sym_bltns(CHEAX *c);

#endif
