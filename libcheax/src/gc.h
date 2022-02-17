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

/* not just GC_H, which conflicts with Boehm in case of USE_BOEHM_GC */
#ifndef CHEAX_GC_H
#define CHEAX_GC_H

#define GC_RUN_THRESHOLD 16384

#include <cheax.h>
#include "api.h"

typedef void (*chx_fin)(void *obj, void *info);

void cheax_gc_init(CHEAX *c);
/*
 * Result must be treated as chx_value. Specifically, gc decisions will
 * be made depending on the value of chx_value::type.
 */
void *cheax_alloc(CHEAX *c, size_t size, int type);
void *cheax_alloc_with_fin(CHEAX *c, size_t size, int type, chx_fin fin, void *info);
void cheax_free(CHEAX *c, void *obj);
void cheax_gc(CHEAX *c);
void cheax_force_gc(CHEAX *c);

#endif