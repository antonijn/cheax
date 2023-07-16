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

#ifndef SYM_H
#define SYM_H

#include <cheax.h>

#include "rbtree.h"

struct full_sym {
	struct chx_id *name;
	bool allow_redef;
	struct chx_sym sym;
};

struct chx_env *norm_env_init(CHEAX *c, struct chx_env *env, struct chx_env *below);
void norm_env_cleanup(struct chx_env *env);

void export_sym_bltns(CHEAX *c);

void def_id(CHEAX *c, struct chx_id *id, struct chx_value value, int flags);
struct chx_sym *defsym_id(CHEAX *c, struct chx_id *id,
                          chx_getter get, chx_setter set,
                          chx_finalizer fin, void *user_info);
struct chx_value get_id(CHEAX *c, struct chx_id *id);
bool try_get_id(CHEAX *c, struct chx_id *id, struct chx_value *out);

#endif
