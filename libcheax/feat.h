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

#ifndef FEAT_H
#define FEAT_H

#include <cheax.h>

enum {
	FILE_IO         = 0x0001,
	SET_STACK_LIMIT = 0x0002,
	GC_BUILTIN      = 0x0004,
	EXIT_BUILTIN    = 0x0008,
	EXPOSE_STDIN    = 0x0010,
	EXPOSE_STDOUT   = 0x0020,
	EXPOSE_STDERR   = 0x0040,
	STDIO           = EXPOSE_STDIN | EXPOSE_STDOUT | EXPOSE_STDERR,
	CONFIG_FEAT_BIT = 0x0080,
	/* bits above CONFIG_FEAT_BIT reserved */

	ALL_FEATURES    = ~0,
};

void cheax_export_bltns_(CHEAX *c);

#endif
