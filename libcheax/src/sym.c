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

#include <cheax.h>
#include <string.h>
#include <stdlib.h>

#include "api.h"
#include "config.h"
#include "setup.h"
#include "gc.h"

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

struct chx_env *
cheax_env(CHEAX *c)
{
	if (c->env != NULL) {
		c->env->base.type &= ~NO_ESC_BIT; /* env escapes! */
		return c->env;
	}

	return &c->globals;
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
	if (has_flag(env->base.type, NO_ESC_BIT))
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
cheax_var(CHEAX *c, const char *id, struct chx_value *value, int flags)
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
	cheax_var(c, id, &cheax_ext_func(c, id, perform, info)->base, CHEAX_READONLY);
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
