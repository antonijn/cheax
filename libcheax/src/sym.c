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

#include <string.h>
#include <stdlib.h>

#include "config.h"
#include "core.h"
#include "err.h"
#include "gc.h"
#include "setup.h"
#include "unpack.h"

static int
full_sym_cmp(struct rb_tree *tree, struct rb_node *a, struct rb_node *b)
{
	struct full_sym *fs_a = a->value, *fs_b = b->value;
	return strcmp(fs_a->name, fs_b->name);
}

/*
 * Returns environment e, with !e->is_bif or e == NULL.
 */
static struct chx_env *
norm_env(struct chx_env *env)
{
	while (env != NULL && env->is_bif)
		env = env->value.bif[0];

	return env;
}

static struct full_sym *
find_sym_in(struct chx_env *env, const char *name)
{
	struct full_sym dummy;
	dummy.name = name;
	env = norm_env(env);

	if (env == NULL)
		return NULL;

	return rb_tree_find(&env->value.norm.syms, &dummy);
}

static struct full_sym *
find_sym_in_or_below(struct chx_env *env, const char *name)
{
	if (env == NULL)
		return NULL;

	if (!env->is_bif) {
		struct full_sym *sym = find_sym_in(env, name);
		if (sym != NULL)
			return sym;

		return find_sym_in_or_below(env->value.norm.below, name);
	}

	for (int i = 0; i < 2; ++i) {
		struct full_sym *sym = find_sym_in_or_below(env->value.bif[i], name);
		if (sym != NULL)
			return sym;
	}

	return NULL;
}

static struct full_sym *
find_sym(CHEAX *c, const char *name)
{
	struct full_sym *fs = find_sym_in_or_below(c->env, name);
	return (fs != NULL) ? fs : find_sym_in(&c->globals, name);
}

struct chx_env *
norm_env_init(CHEAX *c, struct chx_env *env, struct chx_env *below)
{
	rb_tree_init(&env->value.norm.syms, full_sym_cmp, c);
	env->is_bif = false;
	env->value.norm.below = below;
	return env;
}

static void
sym_destroy(CHEAX *c, struct full_sym *fs)
{
	struct chx_sym *sym = &fs->sym;
	if (sym->fin != NULL)
		sym->fin(c, sym);
	cheax_free(c, fs);
}

static void
undef_sym(CHEAX *c, struct chx_env *env, struct full_sym *fs)
{
	if (rb_tree_remove(&env->value.norm.syms, fs))
		sym_destroy(c, fs);
}

static void
full_sym_node_dealloc(struct rb_tree *syms, struct rb_node *node)
{
	sym_destroy(syms->c, node->value);
	rb_node_dealloc(syms->c, node);
}

void
norm_env_cleanup(struct chx_env *env)
{
	rb_tree_cleanup(&env->value.norm.syms, full_sym_node_dealloc);
}

static void
norm_env_fin(void *env_bytes, void *info)
{
	norm_env_cleanup(env_bytes);
}

static void
escape(struct chx_env *env)
{
	if (env == NULL)
		return;

	env->base.rtflags &= ~NO_ESC_BIT;

	if (env->is_bif) {
		for (int i = 0; i < 2; ++i)
			escape(env->value.bif[i]);
	} else {
		escape(env->value.norm.below);
	}
}

struct chx_env *
cheax_env(CHEAX *c)
{
	escape(c->env);
	return (c->env == NULL) ? &c->globals : c->env;
}

struct chx_env *
cheax_push_env(CHEAX *c)
{
	struct chx_env *env = gcol_alloc_with_fin(c, sizeof(struct chx_env), CHEAX_ENV,
	                                          norm_env_fin, NULL);
	return (env == NULL) ? NULL : (c->env = norm_env_init(c, env, c->env));
}

struct chx_env *
cheax_enter_env(CHEAX *c, struct chx_env *main)
{
	struct chx_env *env = gcol_alloc(c, sizeof(struct chx_env), CHEAX_ENV);
	if (env != NULL) {
		env->is_bif = true;
		env->value.bif[0] = main;
		env->value.bif[1] = c->env;
		c->env = env;
	}
	return env;
}

void
cheax_pop_env(CHEAX *c)
{
	struct chx_env *env = c->env;
	if (env == NULL) {
		cry(c, "cheax_pop_env", CHEAX_EAPI, "cannot pop NULL env");
		return;
	}

	if (env->is_bif)
		c->env = env->value.bif[1];
	else
		c->env = env->value.norm.below;

	/* dangerous, but worth it! */
	if (has_flag(env->base.rtflags, NO_ESC_BIT))
		gcol_free(c, env);
}

struct chx_sym *
cheax_defsym(CHEAX *c, const char *id,
             chx_getter get, chx_setter set,
             chx_finalizer fin, void *user_info)
{
	if (id == NULL) {
		cry(c, "cheax_defsym", CHEAX_EAPI, "`id' cannot be NULL");
		return NULL;
	}

	if (get == NULL && set == NULL) {
		cry(c, "cheax_defsym", CHEAX_EAPI, "`get' and `set' cannot both be NULL");
		return NULL;
	}

	struct chx_env *env = norm_env(c->env);
	if (env == NULL)
		env = &c->globals;

	struct full_sym *prev_fs = find_sym_in(env, id);
	if (prev_fs != NULL && !prev_fs->allow_redef) {
		cry(c, "defsym", CHEAX_EEXIST, "symbol `%s' already exists", id);
		return NULL;
	}

	size_t idlen = strlen(id);

	char *fs_mem = cheax_malloc(c, sizeof(struct full_sym) + idlen + 1);
	if (fs_mem == NULL)
		return NULL;
	char *idcpy = fs_mem + sizeof(struct full_sym);
	memcpy(idcpy, id, idlen + 1);

	struct full_sym *fs = (struct full_sym *)fs_mem;
	fs->name = idcpy;
	fs->allow_redef = c->allow_redef && env == &c->globals;
	fs->sym.get = get;
	fs->sym.set = set;
	fs->sym.fin = fin;
	fs->sym.user_info = user_info;
	fs->sym.protect = NULL;

	if (prev_fs != NULL && prev_fs->allow_redef)
		undef_sym(c, env, prev_fs);

	rb_tree_insert(&env->value.norm.syms, fs);
	return &fs->sym;
}

static struct chx_value *
var_get(CHEAX *c, struct chx_sym *sym)
{
	return sym->protect;
}
static void
var_set(CHEAX *c, struct chx_sym *sym, struct chx_value *value)
{
	sym->protect = value;
}

void
cheax_def(CHEAX *c, const char *id, struct chx_value *value, int flags)
{
	struct chx_sym *sym;
	sym = cheax_defsym(c, id,
	                   has_flag(flags, CHEAX_WRITEONLY) ? NULL : var_get,
	                   has_flag(flags, CHEAX_READONLY)  ? NULL : var_set,
	                   NULL, NULL);
	if (sym != NULL)
		sym->protect = value;
}

void
cheax_defmacro(CHEAX *c, const char *id, chx_func_ptr perform, void *info)
{
	cheax_def(c, id, &cheax_ext_func(c, id, perform, info)->base, CHEAX_READONLY);
}

void
cheax_set(CHEAX *c, const char *id, struct chx_value *value)
{
	if (id == NULL) {
		cry(c, "set", CHEAX_EAPI, "`id' cannot be NULL");
		return;
	}

	struct full_sym *fs = find_sym(c, id);
	if (fs == NULL) {
		cry(c, "set", CHEAX_ENOSYM, "no such symbol `%s'", id);
		return;
	}

	struct chx_sym *sym = &fs->sym;
	if (sym->set == NULL)
		cry(c, "set", CHEAX_EREADONLY, "cannot write to read-only symbol");
	else
		sym->set(c, sym, value);
}

struct chx_value *
cheax_get(CHEAX *c, const char *id)
{
	if (id == NULL) {
		cry(c, "get", CHEAX_EAPI, "`id' cannot be NULL");
		return NULL;
	}

	struct full_sym *fs = find_sym(c, id);
	if (fs == NULL) {
		cry(c, "get", CHEAX_ENOSYM, "no such symbol `%s'", id);
		return NULL;
	}

	struct chx_sym *sym = &fs->sym;
	if (sym->get == NULL) {
		cry(c, "set", CHEAX_EWRITEONLY, "cannot read from write-only symbol");
		return NULL;
	}

	return sym->get(c, sym);
}

static struct chx_value *
sync_int_get(CHEAX *c, struct chx_sym *sym)
{
	return &cheax_int(c, *(int *)sym->user_info)->base;
}
static void
sync_int_set(CHEAX *c, struct chx_sym *sym, struct chx_value *value)
{
	if (!try_vtoi(value, sym->user_info))
		cry(c, "set", CHEAX_ETYPE, "invalid type");
}

void
cheax_sync_int(CHEAX *c, const char *name, int *var, int flags)
{
	cheax_defsym(c, name,
	             has_flag(flags, CHEAX_WRITEONLY) ? NULL : sync_int_get,
	             has_flag(flags, CHEAX_READONLY)  ? NULL : sync_int_set,
	             NULL, var);
}

static struct chx_value *
sync_float_get(CHEAX *c, struct chx_sym *sym)
{
	return &cheax_double(c, *(float *)sym->user_info)->base;
}
static void
sync_float_set(CHEAX *c, struct chx_sym *sym, struct chx_value *value)
{
	double d;
	if (!try_vtod(value, &d))
		cry(c, "set", CHEAX_ETYPE, "invalid type");
	else
		*(float *)sym->user_info = d;
}

void
cheax_sync_float(CHEAX *c, const char *name, float *var, int flags)
{
	cheax_defsym(c, name,
	             has_flag(flags, CHEAX_WRITEONLY) ? NULL : sync_float_get,
	             has_flag(flags, CHEAX_READONLY)  ? NULL : sync_float_set,
	             NULL, var);
}

static struct chx_value *
sync_double_get(CHEAX *c, struct chx_sym *sym)
{
	return &cheax_double(c, *(double *)sym->user_info)->base;
}
static void
sync_double_set(CHEAX *c, struct chx_sym *sym, struct chx_value *value)
{
	if (!try_vtod(value, sym->user_info))
		cry(c, "set", CHEAX_ETYPE, "invalid type");
}

void
cheax_sync_double(CHEAX *c, const char *name, double *var, int flags)
{
	cheax_defsym(c, name,
	             has_flag(flags, CHEAX_WRITEONLY) ? NULL : sync_double_get,
	             has_flag(flags, CHEAX_READONLY)  ? NULL : sync_double_set,
	             NULL, var);
}

struct sync_nstring_info {
	char *buf;
	size_t size;
};

static struct chx_value *
sync_nstring_get(CHEAX *c, struct chx_sym *sym)
{
	struct sync_nstring_info *info = sym->user_info;
	return &cheax_string(c, info->buf)->base;
}
static void
sync_nstring_set(CHEAX *c, struct chx_sym *sym, struct chx_value *value)
{
	if (cheax_type_of(value) != CHEAX_STRING) {
		cry(c, "set", CHEAX_ETYPE, "invalid type");
		return;
	}

	struct chx_string *str = (struct chx_string *)value;
	struct sync_nstring_info *info = sym->user_info;
	if (info->size - 1 < str->len) {
		cry(c, "set", CHEAX_EVALUE, "string too big");
		return;
	}

	memcpy(info->buf, str->value, str->len);
	info->buf[str->len] = '\0';
}
static void
sync_nstring_finalizer(CHEAX *c, struct chx_sym *sym)
{
	cheax_free(c, sym->user_info);
}

void
cheax_sync_nstring(CHEAX *c, const char *name, char *buf, size_t size, int flags)
{
	if (buf == NULL) {
		cry(c, "cheax_sync_nstring", CHEAX_EAPI, "`buf' cannot be NULL");
		return;
	}

	if (size == 0) {
		cry(c, "cheax_sync_nstring", CHEAX_EAPI, "`size' cannot be zero");
		return;
	}

	struct sync_nstring_info *info = cheax_malloc(c, sizeof(struct sync_nstring_info));
	if (info == NULL)
		return;

	info->buf = buf;
	info->size = size;

	struct chx_sym *sym;
	sym = cheax_defsym(c, name,
	                   has_flag(flags, CHEAX_WRITEONLY) ? NULL : sync_nstring_get,
	                   has_flag(flags, CHEAX_READONLY)  ? NULL : sync_nstring_set,
	                   sync_nstring_finalizer, info);

	if (sym == NULL)
		cheax_free(c, info);
}

/*
 *  _           _ _ _   _
 * | |__  _   _(_) | |_(_)_ __  ___
 * | '_ \| | | | | | __| | '_ \/ __|
 * | |_) | |_| | | | |_| | | | \__ \
 * |_.__/ \__,_|_|_|\__|_|_| |_|___/
 *
 */

struct defsym_info {
	struct chx_func *get, *set;
};

static void
defgetset(CHEAX *c, const char *name,
          struct chx_value *getset_args,
          struct chx_list *args,
          struct defsym_info *info,
          struct chx_func **out)
{
	if (info == NULL) {
		cry(c, name, CHEAX_EEVAL, "out of symbol scope");
		return;
	}

	if (args == NULL) {
		cry(c, name, CHEAX_EMATCH, "expected body");
		return;
	}

	if (*out != NULL) {
		cry(c, name, CHEAX_EEXIST, "already called");
		return;
	}

	struct chx_func *res = gcol_alloc(c, sizeof(struct chx_func), CHEAX_FUNC);
	if (res != NULL) {
		res->args = getset_args;
		res->body = args;
		res->lexenv = c->env;
	}
	*out = res;
}

static struct chx_value *
bltn_defget(CHEAX *c, struct chx_list *args, void *info)
{
	struct defsym_info *dinfo = info;
	defgetset(c, "defget", NULL, args, dinfo, &dinfo->get);
	return NULL;
}
static struct chx_value *
bltn_defset(CHEAX *c, struct chx_list *args, void *info)
{
	static struct chx_id value_id = { { CHEAX_ID, 0 }, "value" };
	static struct chx_list set_args = { { CHEAX_LIST, 0 }, &value_id.base, NULL };

	struct defsym_info *dinfo = info;
	defgetset(c, "defset", &set_args.base, args, dinfo, &dinfo->set);
	return NULL;
}

static struct chx_value *
defsym_get(CHEAX *c, struct chx_sym *sym)
{
	struct defsym_info *info = sym->user_info;
	struct chx_list sexpr = { { CHEAX_LIST, 0 }, &info->get->base, NULL };
	return cheax_eval(c, &sexpr.base);
}
static void
defsym_set(CHEAX *c, struct chx_sym *sym, struct chx_value *value)
{
	struct defsym_info *info = sym->user_info;
	struct chx_quote arg  = { { CHEAX_QUOTE, 0 }, value };
	struct chx_list args  = { { CHEAX_LIST, 0 }, &arg.base,        NULL  };
	struct chx_list sexpr = { { CHEAX_LIST, 0 }, &info->set->base, &args };
	cheax_eval(c, &sexpr.base);
}
static void
defsym_finalizer(CHEAX *c, struct chx_sym *sym)
{
	cheax_free(c, sym->user_info);
}

static struct chx_value *
bltn_defsym(CHEAX *c, struct chx_list *args, void *info)
{
	if (args == NULL) {
		cry(c, "defsym", CHEAX_EMATCH, "expected symbol name");
		return NULL;
	}

	struct chx_value *idval = args->value;
	if (cheax_type_of(idval) != CHEAX_ID) {
		cry(c, "defsym", CHEAX_ETYPE, "expected identifier");
		return NULL;
	}

	struct chx_id *id = (struct chx_id *)idval;
	bool body_ok = false;

	struct defsym_info *dinfo = cheax_malloc(c, sizeof(struct defsym_info));
	if (dinfo == NULL)
		return NULL;
	dinfo->get = dinfo->set = NULL;

	struct chx_ext_func *defget, *defset;
	defget = cheax_ext_func(c, "defget", bltn_defget, dinfo);
	defset = cheax_ext_func(c, "defset", bltn_defset, dinfo);
	cheax_ft(c, err_pad); /* alloc failure */

	struct chx_env *new_env = cheax_push_env(c);
	if (new_env == NULL)
		goto err_pad;
	new_env->base.rtflags |= NO_ESC_BIT;

	cheax_def(c, "defget", &defget->base, CHEAX_READONLY);
	cheax_def(c, "defset", &defset->base, CHEAX_READONLY);
	cheax_ft(c, pad); /* alloc failure */

	for (struct chx_list *cons = args->next; cons != NULL; cons = cons->next) {
		cheax_eval(c, cons->value);
		cheax_ft(c, pad);
	}

	body_ok = true;
pad:
	defget->info = defset->info = NULL;
	cheax_pop_env(c);

	if (!body_ok)
		goto err_pad;

	if (dinfo->get == NULL && dinfo->set == NULL) {
		cry(c, "defsym", CHEAX_ENOSYM, "symbol must have getter or setter");
		goto err_pad;
	}

	chx_getter act_get = (dinfo->get == NULL) ? NULL : defsym_get;
	chx_setter act_set = (dinfo->set == NULL) ? NULL : defsym_set;
	struct chx_sym *sym = cheax_defsym(c, id->id, act_get, act_set, defsym_finalizer, dinfo);
	if (sym == NULL)
		goto err_pad;

	struct chx_list *protect = NULL;
	if (dinfo->get != NULL)
		protect = cheax_list(c, &dinfo->get->base, protect);
	if (dinfo->set != NULL)
		protect = cheax_list(c, &dinfo->set->base, protect);
	sym->protect = &protect->base;

	return NULL;
err_pad:
	cheax_free(c, dinfo);
	return NULL;
}

static struct chx_value *
bltn_def(CHEAX *c, struct chx_list *args, void *info)
{
	struct chx_value *idval, *setto;
	if (0 == unpack(c, "def", args, "_.", &idval, &setto)
	 && !cheax_match(c, idval, setto, CHEAX_READONLY)
	 && cheax_errno(c) == 0)
	{
		cry(c, "def", CHEAX_EMATCH, "invalid pattern");
	}
	return NULL;
}
static struct chx_value *
bltn_var(CHEAX *c, struct chx_list *args, void *info)
{
	struct chx_value *idval, *setto;
	if (0 == unpack(c, "var", args, "_.?", &idval, &setto)
	 && !cheax_match(c, idval, setto, 0)
	 && cheax_errno(c) == 0)
	{
		cry(c, "var", CHEAX_EMATCH, "invalid pattern");
	}
	return NULL;
}

static struct chx_value *
bltn_let(CHEAX *c, struct chx_list *args, void *info)
{
	struct chx_value *res = NULL;

	if (args == NULL) {
		cry(c, "let", CHEAX_EMATCH, "invalid let");
		return NULL;
	}

	struct chx_value *pairsv = args->value;
	if (cheax_type_of(pairsv) != CHEAX_LIST) {
		cry(c, "let", CHEAX_ETYPE, "invalid let");
		return NULL;
	}

	struct chx_env *new_env = cheax_push_env(c);
	if (new_env == NULL)
		return NULL;
	/* probably won't escape; major memory optimisation */
	new_env->base.rtflags |= NO_ESC_BIT;

	for (struct chx_list *pairs = (struct chx_list *)pairsv;
	     pairs != NULL;
	     pairs = pairs->next)
	{
		struct chx_value *pairv = pairs->value;
		if (cheax_type_of(pairv) != CHEAX_LIST) {
			cry(c, "let", CHEAX_ETYPE, "expected list of lists in first arg");
			goto pad;
		}

		struct chx_list *pair = (struct chx_list *)pairv;
		if (pair->next == NULL || pair->next->next != NULL) {
			cry(c, "let", CHEAX_EVALUE, "expected list of match pairs in first arg");
			goto pad;
		}

		struct chx_value *pan = pair->value, *match = pair->next->value;
		match = cheax_eval(c, match);
		cheax_ft(c, pad);

		if (!cheax_match(c, pan, match, CHEAX_READONLY)) {
			cry(c, "let", CHEAX_EMATCH, "failed match in match pair list");
			goto pad;
		}
	}

	if (args->next == NULL) {
		cry(c, "let", CHEAX_EMATCH, "expected body");
		goto pad;
	}

	struct chx_value *retval;
	for (struct chx_list *cons = args->next; cons != NULL; cons = cons->next) {
		retval = cheax_eval(c, cons->value);
		cheax_ft(c, pad);
	}

	res = retval;
pad:
	cheax_pop_env(c);
	return res;
}

static struct chx_value *
bltn_set(CHEAX *c, struct chx_list *args, void *info)
{
	const char *id;
	struct chx_value *setto;
	if (0 == unpack(c, "set", args, "N!.", &id, &setto))
		cheax_set(c, id, setto);
	return NULL;
}

static struct chx_value *
bltn_env(CHEAX *c, struct chx_list *args, void *info)
{
	return (0 == unpack(c, "env", args, ""))
	     ? &cheax_env(c)->base
	     : NULL;
}

void
export_sym_bltns(CHEAX *c)
{
	cheax_defmacro(c, "defsym", bltn_defsym, NULL);
	cheax_defmacro(c, "var",    bltn_var,    NULL);
	cheax_defmacro(c, "def",    bltn_def,    NULL);
	cheax_defmacro(c, "let",    bltn_let,    NULL);
	cheax_defmacro(c, "set",    bltn_set,    NULL);
	cheax_defmacro(c, "env",    bltn_env,    NULL);
}
