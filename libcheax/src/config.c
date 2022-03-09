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

#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "api.h"

int
cheax_config_get_int(CHEAX *c, const char *opt)
{
	struct config_info *ci = find_opt(opt);
	if (ci == NULL) {
		cry(c, "cheax_config_get_int", CHEAX_EAPI, "invalid option `%s'", opt);
		return 0;
	}

	if (ci->type != CHEAX_INT) {
		cry(c, "cheax_config_get_int", CHEAX_EAPI, "wrong option type for `%s'", opt);
		return 0;
	}

	return ci->get.get_int(c);
}
int
cheax_config_int(CHEAX *c, const char *opt, int value)
{
	struct config_info *ci = find_opt(opt);
	if (ci == NULL || ci->type != CHEAX_INT)
		return -1;

	ci->set.set_int(c, value);
	return 0;
}

bool
cheax_config_get_bool(CHEAX *c, const char *opt)
{
	struct config_info *ci = find_opt(opt);
	if (ci == NULL) {
		cry(c, "cheax_config_get_bool", CHEAX_EAPI, "invalid option `%s'", opt);
		return 0;
	}

	if (ci->type != CHEAX_BOOL) {
		cry(c, "cheax_config_get_bool", CHEAX_EAPI, "wrong option type for `%s'", opt);
		return 0;
	}

	return ci->get.get_bool(c);
}
int
cheax_config_bool(CHEAX *c, const char *opt, bool value)
{
	struct config_info *ci = find_opt(opt);
	if (ci == NULL || ci->type != CHEAX_BOOL)
		return -1;

	ci->set.set_bool(c, value);
	return 0;
}

static int
get_stack_limit(CHEAX *c)
{
	return c->stack_limit;
}
static void
set_stack_limit(CHEAX *c, int value)
{
	static const int min_stack_limit = 16;
	if (value != 0 && value < min_stack_limit) {
		cry(c, "stack-limit", CHEAX_EAPI,
		    "stack limit must be zero or at least %d", min_stack_limit);
	} else {
		c->stack_limit = value;
	}
}

static int
get_mem_limit(CHEAX *c)
{
	return c->mem_limit;
}
static void
set_mem_limit(CHEAX *c, int value)
{
	static const int min_mem_limit = 0x40000;
	if (value != 0 && value < min_mem_limit) {
		cry(c, "mem-limit", CHEAX_EAPI,
		    "memory limit must be zero or at least %d", min_mem_limit);
	} else {
		c->mem_limit = value;
	}
}

/* sorted asciibetically for use in bsearch() */
struct config_info opts[] = {
	{
		"mem-limit",
		CHEAX_INT,
		{ .get_int = get_mem_limit },
		{ .set_int = set_mem_limit }
	},
	{
		"stack-limit",
		CHEAX_INT,
		{ .get_int = get_stack_limit },
		{ .set_int = set_stack_limit }
	},
};

/* used in bsearch() */
static int
config_info_compar(const char *key, const struct config_info *ci)
{
	return strcmp(key, ci->name);
}

struct config_info *find_opt(const char *name)
{
	return bsearch(name, opts, sizeof(opts) / sizeof(opts[0]), sizeof(opts[0]),
	               (int (*)(const void *, const void *))config_info_compar);
}
