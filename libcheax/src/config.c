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
#include "core.h"
#include "err.h"
#include "feat.h"

union config_get {
	int (*get_int)(CHEAX *c);
	bool (*get_bool)(CHEAX *c);
};

union config_set {
	void (*set_int)(CHEAX *c, int value);
	void (*set_bool)(CHEAX *c, bool value);
};

struct config_info {
	const char *name;
	int type;
	const char *metavar;
	union config_get get;
	union config_set set;
	const char *help;
};

static bool
get_allow_redef(CHEAX *c)
{
	return c->allow_redef;
}
static void
set_allow_redef(CHEAX *c, bool value)
{
	c->allow_redef = value;
}

static int
get_bt_limit(CHEAX *c)
{
	return c->bt.limit;
}
static void
set_bt_limit(CHEAX *c, int value)
{
	static const int max_bt_limit = 256;
	if (value < 0)
		cheax_throwf(c, CHEAX_EAPI, "backtrace limit must be non-negative");
	else if (value > max_bt_limit)
		cheax_throwf(c, CHEAX_EAPI, "backtrace limit must be at most %d", max_bt_limit);
	else
		bt_limit(c, value);
}

static bool
get_gen_debug_info(CHEAX *c)
{
	return c->gen_debug_info;
}
static void
set_gen_debug_info(CHEAX *c, bool value)
{
	c->gen_debug_info = value;
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
	if (value != 0 && value < min_stack_limit)
		cheax_throwf(c, CHEAX_EAPI, "stack limit must be zero or at least %d", min_stack_limit);
	else
		c->stack_limit = value;
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
	if (value != 0 && value < min_mem_limit)
		cheax_throwf(c, CHEAX_EAPI, "memory limit must be zero or at least %d", min_mem_limit);
	else
		c->mem_limit = value;
}

/* sorted asciibetically for use in bsearch() */
static struct config_info opts[] = {
	{
		"allow-redef", CHEAX_BOOL, "<true|false>",
		{ .get_bool = get_allow_redef },
		{ .set_bool = set_allow_redef },
		"Allow symbol redefinition in global scope."
	},
	{
		"bt-limit", CHEAX_INT, "N",
		{ .get_int = get_bt_limit },
		{ .set_int = set_bt_limit },
		"Backtrace length limit."
	},
	{
		"gen-debug-info", CHEAX_BOOL, "<true|false>",
		{ .get_bool = get_gen_debug_info },
		{ .set_bool = set_gen_debug_info },
		"Generate debug info when reading S-expressions to "
		"improve backtrace readability."
	},
	{
		"mem-limit", CHEAX_INT, "N",
		{ .get_int = get_mem_limit },
		{ .set_int = set_mem_limit },
		"Maximum amount of memory that cheax is allowed to use "
		"given as a number of bytes. Set to 0 to disable "
		"memory limiting."
	},
	{
		"stack-limit", CHEAX_INT, "N",
		{ .get_int = get_stack_limit },
		{ .set_int = set_stack_limit },
		"Maximum call stack depth. Set to 0 to disable stack "
		"depth limiting."
	},
};

/* used in bsearch() */
static int
config_info_compar(const char *key, const struct config_info *ci)
{
	return strcmp(key, ci->name);
}

static struct config_info *
find_opt(const char *name)
{
	return bsearch(name, opts, sizeof(opts) / sizeof(opts[0]), sizeof(opts[0]),
	               (int (*)(const void *, const void *))config_info_compar);
}

static struct chx_value *
config_sym_get(CHEAX *c, struct chx_sym *sym)
{
	struct config_info *ci = sym->user_info;
	switch (ci->type) {
	case CHEAX_INT:
		return &cheax_int(c, ci->get.get_int(c))->base;
	case CHEAX_BOOL:
		return &cheax_bool(c, ci->get.get_bool(c))->base;
	default:
		cheax_throwf(c, CHEAX_EEVAL, "config_sym_get(): internal error");
		return NULL;
	}
}
static void
config_sym_set(CHEAX *c, struct chx_sym *sym, struct chx_value *value)
{
	struct config_info *ci = sym->user_info;
	int i;

	switch (ci->type) {
	case CHEAX_INT:
		if (try_vtoi(value, &i))
			ci->set.set_int(c, i);
		else
			cheax_throwf(c, CHEAX_ETYPE, "invalid type");
		break;
	case CHEAX_BOOL:
		if (cheax_type_of(value) == CHEAX_BOOL)
			ci->set.set_bool(c, ((struct chx_int *)value)->value);
		else
			cheax_throwf(c, CHEAX_ETYPE, "invalid type");
		break;
	default:
		cheax_throwf(c, CHEAX_EEVAL, "config_sym_set(): internal error");
		return;
	}

	/* convert EAPI to EVALUE */
	if (cheax_errno(c) == CHEAX_EAPI)
		cheax_throw(c, CHEAX_EVALUE, c->error.msg);
}

int
config_init(CHEAX *c)
{
	size_t nopts = sizeof(opts) / sizeof(opts[0]);
	c->config_syms = malloc(nopts * sizeof(struct chx_sym *));
	if (c->config_syms == NULL)
		return -1;

	for (size_t i = 0; i < nopts; ++i) {
		c->config_syms[i] = cheax_defsym(c, opts[i].name,
		                                 config_sym_get, NULL, NULL,
		                                 &opts[i]);
		cheax_ft(c, pad);
	}

	return 0;
pad:
	return -1;
}

int
find_config_feature(const char *feat)
{
	if (strncmp(feat, "set-", 4) != 0)
		return 0;

	struct config_info *ci = find_opt(feat + 4);
	return (ci != NULL) ? (CONFIG_FEAT_BIT << (ci - opts)) : 0;
}

void
load_config_feature(CHEAX *c, int bits)
{
	int nopts = sizeof(opts) / sizeof(opts[0]);
	for (int i = 0; i < nopts; ++i)
		if (has_flag(bits, CONFIG_FEAT_BIT << i))
			c->config_syms[i]->set = config_sym_set;
}

struct chx_list *
config_feature_list(CHEAX *c, struct chx_list *base)
{
	struct chx_list *list = base;
	int nopts = sizeof(opts) / sizeof(opts[0]);
	for (int i = nopts - 1; i >= 0; --i) {
		if (has_flag(c->features, CONFIG_FEAT_BIT << i)) {
			char buf[128];
			sprintf(buf, "set-%s", opts[i].name);
			list = cheax_list(c, &cheax_string(c, buf)->base, list);
		}
	}
	return list;
}

int
cheax_config_get_int(CHEAX *c, const char *opt)
{
	struct config_info *ci = find_opt(opt);
	if (ci == NULL) {
		cheax_throwf(c, CHEAX_EAPI, "config_get_int(): invalid option `%s'", opt);
		return 0;
	}

	if (ci->type != CHEAX_INT) {
		cheax_throwf(c, CHEAX_EAPI, "config_get_int(): wrong option type for `%s'", opt);
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
		cheax_throwf(c, CHEAX_EAPI, "config_get_bool(): invalid option `%s'", opt);
		return 0;
	}

	if (ci->type != CHEAX_BOOL) {
		cheax_throwf(c, CHEAX_EAPI, "config_get_bool(): wrong option type for `%s'", opt);
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

int
cheax_config_help(struct chx_config_help **help, size_t *num_opts)
{
	size_t nopts = sizeof(opts) / sizeof(opts[0]);
	struct chx_config_help *arr = malloc(nopts * sizeof(struct chx_config_help));
	if (arr == NULL) {
		*help = NULL;
		*num_opts = 0;
		return -1;
	}

	for (size_t i = 0; i < nopts; ++i) {
		arr[i].name = opts[i].name;
		arr[i].type = opts[i].type;
		arr[i].metavar = opts[i].metavar;
		arr[i].help = opts[i].help;
	}

	*help = arr;
	*num_opts = nopts;
	return 0;
}
