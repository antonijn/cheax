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

#ifndef CINFO_H
#define CINFO_H

#include <stdbool.h>

/* ascii-only, locale-invariant, portable versions of ctype.h functions */

bool cheax_isdigit_(int c);
bool cheax_isspace_(int c);
bool cheax_isgraph_(int c);
bool cheax_isprint_(int c);

/* whether c is valid character in identifier */
bool cheax_isid_(int c);

/* whether c is valid character for starting an identifier */
bool cheax_isid_initial_(int c);

/* ascii character to integer digit */
int cheax_todigit_(int c, int base);

#endif
