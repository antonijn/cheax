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

#ifndef ERR_H
#define ERR_H

#include <cheax.h>

int bt_init(CHEAX *c, size_t limit);
int bt_limit(CHEAX *c, size_t limit);
void bt_add_tail_msg(CHEAX *c, int tail_lvls);
void bt_print(CHEAX *c);

/*
 * Helper function: calls cheax_add_bt(c) and returns NULL if
 * cheax_errno(c) is set, just returns `v' otherwise.
 */
struct chx_value bt_wrap(CHEAX *c, struct chx_value v);

void export_err_bltns(CHEAX *c);

#endif
