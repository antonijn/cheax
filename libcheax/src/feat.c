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

#include <stdlib.h>
#include <string.h>

#include "arith.h"
#include "config.h"
#include "core.h"
#include "err.h"
#include "eval.h"
#include "feat.h"
#include "format.h"
#include "gc.h"
#include "maths.h"
#include "io.h"
#include "sym.h"
#include "unpack.h"

/* sorted asciibetically for use in bsearch() */
static const struct nfeat { const char *name; int feat; } named_feats[] = {
	{"all",     ALL_FEATURES  },
	{"exit",    EXIT_BUILTIN  },
	{"file-io", FILE_IO       },
	{"gc",      GC_BUILTIN    },
	{"stderr",  EXPOSE_STDERR },
	{"stdin",   EXPOSE_STDIN  },
	{"stdio",   STDIO         },
	{"stdout",  EXPOSE_STDOUT },
};

/* used in bsearch() */
static int
feature_compar(const char *key, const struct nfeat *nf)
{
	return strcmp(key, nf->name);
}

static struct chx_list *
feature_list(CHEAX *c, struct chx_list *base)
{
	struct chx_list *list = base;
	int len = sizeof(named_feats) / sizeof(named_feats[0]);
	for (int i = len - 1; i >= 0; --i)
		if (has_flag(c->features, named_feats[i].feat))
			list = cheax_list(c, cheax_string(c, named_feats[i].name), list).data.as_list;
	return list;
}

static struct chx_value
get_features(CHEAX *c, struct chx_sym *sym)
{
	return cheax_list_value(feature_list(c, config_feature_list(c, NULL)));
}

static int
find_feature(const char *feat)
{
	struct nfeat *res;
	res = bsearch(feat, named_feats,
	              sizeof(named_feats) / sizeof(named_feats[0]), sizeof(named_feats[0]),
	              (int (*)(const void *, const void *))feature_compar);
	return (res == NULL) ? 0 : res->feat;
}

static struct chx_value
bltn_exit(CHEAX *c, struct chx_list *args, void *info)
{
	struct chx_value code_val;
	if (unpack(c, args, "I?", &code_val) < 0)
		return CHEAX_NIL;

	exit(cheax_is_nil(code_val) ? 0 : (int)code_val.data.as_int);
}

int
cheax_load_feature(CHEAX *c, const char *feat)
{
	int feats = find_feature(feat) | find_config_feature(feat);
	if (feats == 0)
		return -1;

	/* newly set features */
	int nf = feats & ~c->features;

	if (has_flag(nf, EXIT_BUILTIN))
		cheax_defun(c, "exit", bltn_exit, NULL);

	load_config_feature(c, nf);
	load_gc_feature(c, nf);
	load_io_feature(c, nf);

	c->features |= nf;
	return 0;
}

void
export_bltns(CHEAX *c)
{
	export_arith_bltns(c);
	export_core_bltns(c);
	export_err_bltns(c);
	export_eval_bltns(c);
	export_format_bltns(c);
	export_io_bltns(c);
	export_math_bltns(c);
	export_sym_bltns(c);

	cheax_defsym(c, "features", get_features, NULL, NULL, NULL);
}
