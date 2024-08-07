/* Copyright (c) 2024, Antonie Blom
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

#ifndef UNPACK_H
#define UNPACK_H

#include <cheax.h>

/*
 * Unpack argument list into several variables, performing type checks
 * and argument evaluation according to a (somewhat) regular expression.
 *
 * Example:
 *
 *   int i1, i2;
 *   struct chx_value i3;
 *   cheax_unpack_(c, <arg-list>, "iIi?", &i1, &i2, &i3);
 *
 * Will unpack an argument list of two or three integers into i1, i2
 * and i3, where the first argument gets evaluated (lowercase "i"
 * specifier) and its integer data stored in i1. The second argument is
 * _not_ evaluated (uppercase "I" specifier), and its integer data
 * stored in i2. The optional ("?" modifier) third argument gets stored
 * in i3 if present, otherwise i3 is set to CHEAX_NIL.
 *
 * Example:
 *
 *   const char *id;
 *   struct chx_list *nums;
 *   cheax_unpack_(c, <arg-list>, "N![id]+", &id, &nums);
 *
 * Will unpack an argument list of one identifier followed by at least
 * one integer or double. The identifier argument is not evaluated
 * (uppercase "N") and the identifier's value is stored as a const char
 * pointer in id ("!" modifier). The following arguments ("+" modifier),
 * which must be either ("[...]") integers ("i") or doubles ("d"), are
 * evaluated and stored as a chx_list pointer in nums.
 *
 * For more examples, see builtins.c. For an exact specification of
 * field specifiers and their meanings, see the variable `unpack_fields'
 * in unpack.c.
 *
 * Returns 0 on success, -1 on failure.
 */
int cheax_unpack_(CHEAX *c, struct chx_list *args, const char *fmt, ...);

enum {
	PP_NIL         = 0x00,
	PP_NODE        = 0x01,
	PP_SEQ         = 0x02,
	PP_MAYBE       = 0x03,
	PP_LIT         = 0x04,
	PP_EXPR        = 0x05,
	PP_INSTR_BITS  = 0x0F,
	PP_ERRMSG_BITS = 0xF0,
};

#define PP_ERRMSG_OFS 4
#define PP_ERR(n) ((((n) + 1) << PP_ERRMSG_OFS) & PP_ERRMSG_BITS)

struct chx_value cheax_preproc_pattern_(CHEAX *c,
                                        struct chx_value input,
                                        const uint8_t *prog,
                                        const char **errors);

#endif
