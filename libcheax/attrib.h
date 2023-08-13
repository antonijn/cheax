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

#ifndef ATTRIB_H
#define ATTRIB_H

#include <cheax.h>

#include "htab.h"

enum attrib_kind {
	ATTRIB_ORIG_FORM,
	ATTRIB_LOC,
	ATTRIB_DOC,

	ATTRIB_FIRST = ATTRIB_ORIG_FORM,
	ATTRIB_LAST  = ATTRIB_DOC,
};

struct attrib_loc {
	const char *file;
	int pos, line;
};

struct attrib {
	struct htab_entry entry;
	void *key;
	union {
		struct chx_string *doc;
		struct chx_list *orig_form;
		struct attrib_loc loc;
	};
};

typedef struct {
	struct htab table;
	size_t size;
} attrib_info[ATTRIB_LAST + 1];

void cheax_attrib_init_(CHEAX *c);
void cheax_attrib_cleanup_(CHEAX *c);

struct attrib *cheax_attrib_add_(CHEAX *c, void *key, enum attrib_kind kind);
void cheax_attrib_remove_(CHEAX *c, void *key, enum attrib_kind kind);
void cheax_attrib_remove_all_(CHEAX *c, void *key);
struct attrib *cheax_attrib_get_(CHEAX *c, void *key, enum attrib_kind kind);
struct attrib *cheax_attrib_copy_(CHEAX *c, void *dst_key, void *src_key, enum attrib_kind kind);

#endif
