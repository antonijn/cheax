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

#ifndef GC_H
#define GC_H

#define GC_RUN_THRESHOLD 0x20000

#include <cheax.h>

#include <stdint.h>

#include "types.h"

typedef void (*chx_fin)(CHEAX *c, void *obj);

struct gc_header_node {
	struct gc_header_node *prev, *next;
};

struct gc_info {
	struct gc_header_node objects;
	chx_fin finalizers[CHEAX_LAST_BASIC_TYPE + 1];
	size_t all_mem, prev_run, num_objects;
	bool lock, triggered;
};

void cheax_gc_init_(CHEAX *c);
void cheax_gc_cleanup_(CHEAX *c);
void *cheax_gc_alloc_(CHEAX *c, size_t size, int type);
void cheax_gc_free_(CHEAX *c, void *obj);
void cheax_gc_register_finalizer_(CHEAX *c, int type, chx_fin fin);

void cheax_gc(CHEAX *c);
void cheax_force_gc(CHEAX *c);

void cheax_load_gc_feature_(CHEAX *c, int bits);

#endif
