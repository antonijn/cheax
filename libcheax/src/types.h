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

#ifndef TYPES_H
#define TYPES_H

#include <cheax.h>

#include <stddef.h>

#include "htab.h"

/*
 * Taken from the Wikipedia (definitely not how-to-style) article
 * https://en.wikipedia.org/w/index.php?title=Offsetof&oldid=1052498292
 */
#define container_of(ptr, type, member) \
	((type *)((char *)(1 ? (ptr) : &((type *)0)->member) - offsetof(type, member)))

struct id_entry {
	struct chx_id id;
	struct htab_entry entry;
	uint32_t hash;
	char value[1];
};

struct chx_special_op {
	unsigned rtflags;
	const char *name;
	chx_tail_func_ptr perform;
	chx_func_ptr preproc;
	void *info;
};

struct chx_string {
	unsigned rtflags;
	char *value;
	size_t len;
	struct chx_string *orig;
};

struct chx_env {
	unsigned rtflags;
	bool is_bif;
	union {
		struct chx_env *bif[2];

		struct {
			struct htab syms;
			struct chx_env *below;
		} norm;
	} value;
};

union chx_any {
	struct chx_list list;
	struct chx_id id;
	struct chx_string string;
	struct chx_quote quote;
	struct chx_func func;
	struct chx_ext_func ext_func;
	struct chx_special_op special_op;
	struct chx_env env;
	unsigned rtflags;
};

#endif
