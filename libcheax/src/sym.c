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

#include <string.h>
#include <stdlib.h>

#include "config.h"
#include "core.h"
#include "err.h"
#include "gc.h"
#include "setup.h"
#include "types.h"
#include "unpack.h"

static uint32_t
full_sym_hash(const struct htab_entry *item)
{
	const struct full_sym *fs = container_of(item, struct full_sym, entry);
	return container_of(fs->name, struct id_entry, id)->hash;
}

static bool
full_sym_eq(const struct htab_entry *ent_a, const struct htab_entry *ent_b)
{
	const struct full_sym *a, *b;
	a = container_of(ent_a, struct full_sym, entry);
	b = container_of(ent_b, struct full_sym, entry);
	return a->name == b->name;
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
find_sym_in(struct chx_env *env, struct chx_id *name)
{
	struct full_sym dummy;
	dummy.name = name;
	env = norm_env(env);

	if (env == NULL)
		return NULL;

	struct htab_search search = htab_get(&env->value.norm.syms, &dummy.entry);
	return (search.item == NULL) ? NULL : container_of(search.item, struct full_sym, entry);
}

static struct full_sym *
find_sym_in_or_below(struct chx_env *env, struct chx_id *name)
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
find_sym(CHEAX *c, struct chx_id *name)
{
	struct full_sym *fs = find_sym_in_or_below(c->env, name);
	return (fs != NULL) ? fs : find_sym_in(c->global_env, name);
}

struct chx_env *
norm_env_init(CHEAX *c, struct chx_env *env, struct chx_env *below)
{
	htab_init(c, &env->value.norm.syms, full_sym_hash, full_sym_eq);
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
	struct htab_search search;
	htab_remove(&env->value.norm.syms, (search = htab_get(&env->value.norm.syms, &fs->entry)));
	if (search.item != NULL)
		sym_destroy(c, fs);
}

static void
sym_destroy_in_htab(struct htab_entry *fs, void *c)
{
	sym_destroy(c, container_of(fs, struct full_sym, entry));
}

void
norm_env_cleanup(CHEAX *c, struct chx_env *env)
{
	htab_cleanup(&env->value.norm.syms, sym_destroy_in_htab, c);
}

void
env_fin(CHEAX *c, void *obj)
{
	(void)c;
	struct chx_env *env = obj;
	if (!env->is_bif)
		norm_env_cleanup(c, env);
}

static void
escape(struct chx_env *env)
{
	if (env == NULL)
		return;

	env->rtflags &= ~NO_ESC_BIT;

	if (env->is_bif) {
		for (int i = 0; i < 2; ++i)
			escape(env->value.bif[i]);
	} else {
		escape(env->value.norm.below);
	}
}

struct chx_value
cheax_env(CHEAX *c)
{
	escape(c->env);
	return (c->env == NULL)
	     ? CHEAX_NIL
	     : cheax_env_value(c->env);
}

void
cheax_push_env(CHEAX *c)
{
	struct chx_env *env = gc_alloc(c, sizeof(struct chx_env), CHEAX_ENV);
	if (env != NULL) {
		env->rtflags |= NO_ESC_BIT;
		c->env = norm_env_init(c, env, c->env);
	}
}

void
cheax_enter_env(CHEAX *c, struct chx_env *main)
{
	struct chx_env *env = gc_alloc(c, sizeof(struct chx_env), CHEAX_ENV);
	if (env != NULL) {
		env->rtflags |= NO_ESC_BIT;
		env->is_bif = true;
		env->value.bif[0] = main;
		env->value.bif[1] = c->env;
		c->env = env;
	}
}

void
cheax_pop_env(CHEAX *c)
{
	struct chx_env *env = c->env;
	if (env == NULL) {
		cheax_throwf(c, CHEAX_EAPI, "pop_env(): cannot pop NULL env");
		return;
	}

	if (env->is_bif)
		c->env = env->value.bif[1];
	else
		c->env = env->value.norm.below;

	/* dangerous, but worth it! */
	if (has_flag(env->rtflags, NO_ESC_BIT))
		gc_free(c, env);
}

struct chx_sym *
defsym_id(CHEAX *c, struct chx_id *id,
          chx_getter get, chx_setter set,
          chx_finalizer fin, void *user_info)
{
	ASSERT_NOT_NULL("defsym", id, NULL);

	if (get == NULL && set == NULL) {
		cheax_throwf(c, CHEAX_EAPI, "defsym(): `get' and `set' cannot both be NULL");
		return NULL;
	}

	struct chx_env *env = norm_env(c->env);
	if (env == NULL)
		env = c->global_env;

	struct full_sym *prev_fs = find_sym_in(env, id);
	if (prev_fs != NULL && !prev_fs->allow_redef) {
		cheax_throwf(c, CHEAX_EEXIST, "symbol `%s' already exists", id->value);
		return NULL;
	}

	struct full_sym *fs = cheax_malloc(c, sizeof(struct full_sym));
	if (fs == NULL)
		return NULL;

	fs->name = id;
	fs->allow_redef = c->allow_redef && (env == c->global_env);
	fs->sym.get = get;
	fs->sym.set = set;
	fs->sym.fin = fin;
	fs->sym.user_info = user_info;
	fs->sym.protect = CHEAX_NIL;

	if (prev_fs != NULL && prev_fs->allow_redef)
		undef_sym(c, env, prev_fs);

	htab_set(&env->value.norm.syms, htab_get(&env->value.norm.syms, &fs->entry), &fs->entry);

	return &fs->sym;
}

struct chx_sym *
cheax_defsym(CHEAX *c, const char *name,
             chx_getter get, chx_setter set,
             chx_finalizer fin, void *user_info)
{
	ASSERT_NOT_NULL("defsym", name, NULL);

	struct chx_id *id = cheax_id(c, name).data.as_id;
	return id != NULL
	     ? defsym_id(c, id, get, set, fin, user_info)
	     : NULL;
}

static struct chx_value
var_get(CHEAX *c, struct chx_sym *sym)
{
	return sym->protect;
}
static void
var_set(CHEAX *c, struct chx_sym *sym, struct chx_value value)
{
	sym->protect = value;
}

void
def_id(CHEAX *c, struct chx_id *id, struct chx_value value, int flags)
{
	struct chx_sym *sym;
	sym = defsym_id(c, id,
	                has_flag(flags, CHEAX_WRITEONLY) ? NULL : var_get,
	                has_flag(flags, CHEAX_READONLY)  ? NULL : var_set,
	                NULL, NULL);
	if (sym != NULL)
		sym->protect = value;
}

void
cheax_def(CHEAX *c, const char *name, struct chx_value value, int flags)
{
	ASSERT_NOT_NULL_VOID("def", name);

	struct chx_id *id = cheax_id(c, name).data.as_id;
	if (id != NULL)
		def_id(c, id, value, flags);
}

void
cheax_defun(CHEAX *c, const char *id, chx_func_ptr perform, void *info)
{
	cheax_def(c, id, cheax_ext_func(c, id, perform, info), CHEAX_READONLY);
}
void
cheax_defsyntax(CHEAX *c,
                const char *id,
                chx_tail_func_ptr perform,
                chx_func_ptr preproc,
                void *info)
{
	struct chx_value specop;
	specop.type = CHEAX_SPECIAL_OP;
	specop.data.as_special_op = gc_alloc(c, sizeof(struct chx_special_op), CHEAX_SPECIAL_OP);
	if (specop.data.as_special_op == NULL)
		return;

	specop.data.as_special_op->name = id;
	specop.data.as_special_op->perform = perform;
	specop.data.as_special_op->preproc = preproc;
	specop.data.as_special_op->info = info;

	struct chx_env *prev_env = c->env;
	c->env = &c->specop_ns;
	cheax_def(c, id, specop, CHEAX_READONLY);
	c->env = prev_env;
}

void
cheax_set(CHEAX *c, const char *name, struct chx_value value)
{
	ASSERT_NOT_NULL_VOID("set", name);

	struct chx_id *id = find_id(c, name);
	struct full_sym *fs;
	if (id == NULL || (fs = find_sym(c, id)) == NULL) {
		cheax_throwf(c, CHEAX_ENOSYM, "no such symbol `%s'", name);
		return;
	}

	struct chx_sym *sym = &fs->sym;
	if (sym->set == NULL)
		cheax_throwf(c, CHEAX_EREADONLY, "cannot write to read-only symbol");
	else
		sym->set(c, sym, value);
}

struct chx_value
get_id(CHEAX *c, struct chx_id *id)
{
	struct chx_value res = CHEAX_NIL;

	if (!try_get_id(c, id, &res) && cheax_errno(c) == 0)
		cheax_throwf(c, CHEAX_ENOSYM, "no such symbol `%s'", id->value);

	return res;
}

struct chx_value
cheax_get(CHEAX *c, const char *name)
{
	ASSERT_NOT_NULL("get", name, CHEAX_NIL);

	struct chx_id *id = find_id(c, name);
	if (id == NULL) {
		cheax_throwf(c, CHEAX_ENOSYM, "no such symbol `%s'", name);
		return CHEAX_NIL;
	}

	return get_id(c, id);
}

bool
try_get_id(CHEAX *c, struct chx_id *id, struct chx_value *out)
{
	ASSERT_NOT_NULL("get", id, false);

	struct full_sym *fs = find_sym(c, id);
	if (fs == NULL)
		return false;

	struct chx_sym *sym = &fs->sym;
	if (sym->get == NULL) {
		cheax_throwf(c, CHEAX_EWRITEONLY, "cannot read from write-only symbol");
		return false;
	}

	*out = sym->get(c, sym);
	return cheax_errno(c) == 0;
}

bool
cheax_try_get(CHEAX *c, const char *name, struct chx_value *out)
{
	ASSERT_NOT_NULL("get", name, false);

	struct chx_id *id = find_id(c, name);
	return id != NULL && try_get_id(c, id, out);
}

struct chx_value
cheax_get_from(CHEAX *c, struct chx_env *env, const char *id)
{
	struct chx_value res = CHEAX_NIL;

	if (!cheax_try_get_from(c, env, id, &res) && cheax_errno(c) == 0)
		cheax_throwf(c, CHEAX_ENOSYM, "no such symbol `%s'", id);

	return res;
}

bool
cheax_try_get_from(CHEAX *c, struct chx_env *env, const char *name, struct chx_value *out)
{
	ASSERT_NOT_NULL("get_from", name, false);

	struct chx_id *id = find_id(c, name);
	if (id == NULL)
		return false;

	struct full_sym *fs = find_sym_in(norm_env(env), id);
	if (fs == NULL)
		return false;

	struct chx_sym *sym = &fs->sym;
	if (sym->get == NULL) {
		cheax_throwf(c, CHEAX_EWRITEONLY, "cannot read from write-only symbol");
		return false;
	}

	*out = sym->get(c, sym);
	return cheax_errno(c) == 0;
}

static struct chx_value
sync_int_get(CHEAX *c, struct chx_sym *sym)
{
	return cheax_int(*(int *)sym->user_info);
}
static void
sync_int_set(CHEAX *c, struct chx_sym *sym, struct chx_value value)
{
	if (!try_vtoi(value, sym->user_info))
		cheax_throwf(c, CHEAX_ETYPE, "invalid type");
}

void
cheax_sync_int(CHEAX *c, const char *name, int *var, int flags)
{
	ASSERT_NOT_NULL_VOID("sync_int", var);
	cheax_defsym(c, name,
	             has_flag(flags, CHEAX_WRITEONLY) ? NULL : sync_int_get,
	             has_flag(flags, CHEAX_READONLY)  ? NULL : sync_int_set,
	             NULL, var);
}

static struct chx_value
sync_bool_get(CHEAX *c, struct chx_sym *sym)
{
	return cheax_bool(*(bool *)sym->user_info);
}
static void
sync_bool_set(CHEAX *c, struct chx_sym *sym, struct chx_value value)
{
	if (value.type != CHEAX_BOOL)
		cheax_throwf(c, CHEAX_ETYPE, "invalid type");
	else
		*(bool *)sym->user_info = (value.data.as_int != 0);
}

void
cheax_sync_bool(CHEAX *c, const char *name, bool *var, int flags)
{
	ASSERT_NOT_NULL_VOID("sync_bool", var);
	cheax_defsym(c, name,
	             has_flag(flags, CHEAX_WRITEONLY) ? NULL : sync_bool_get,
	             has_flag(flags, CHEAX_READONLY)  ? NULL : sync_bool_set,
	             NULL, var);
}

static struct chx_value
sync_float_get(CHEAX *c, struct chx_sym *sym)
{
	return cheax_double(*(float *)sym->user_info);
}
static void
sync_float_set(CHEAX *c, struct chx_sym *sym, struct chx_value value)
{
	double d;
	if (!try_vtod(value, &d))
		cheax_throwf(c, CHEAX_ETYPE, "invalid type");
	else
		*(float *)sym->user_info = d;
}

void
cheax_sync_float(CHEAX *c, const char *name, float *var, int flags)
{
	ASSERT_NOT_NULL_VOID("sync_float", var);
	cheax_defsym(c, name,
	             has_flag(flags, CHEAX_WRITEONLY) ? NULL : sync_float_get,
	             has_flag(flags, CHEAX_READONLY)  ? NULL : sync_float_set,
	             NULL, var);
}

static struct chx_value
sync_double_get(CHEAX *c, struct chx_sym *sym)
{
	return cheax_double(*(double *)sym->user_info);
}
static void
sync_double_set(CHEAX *c, struct chx_sym *sym, struct chx_value value)
{
	if (!try_vtod(value, sym->user_info))
		cheax_throwf(c, CHEAX_ETYPE, "invalid type");
}

void
cheax_sync_double(CHEAX *c, const char *name, double *var, int flags)
{
	ASSERT_NOT_NULL_VOID("sync_double", var);
	cheax_defsym(c, name,
	             has_flag(flags, CHEAX_WRITEONLY) ? NULL : sync_double_get,
	             has_flag(flags, CHEAX_READONLY)  ? NULL : sync_double_set,
	             NULL, var);
}

struct sync_nstring_info {
	char *buf;
	size_t size;
};

static struct chx_value
sync_nstring_get(CHEAX *c, struct chx_sym *sym)
{
	struct sync_nstring_info *info = sym->user_info;
	return cheax_string(c, info->buf);
}
static void
sync_nstring_set(CHEAX *c, struct chx_sym *sym, struct chx_value value)
{
	if (value.type != CHEAX_STRING) {
		cheax_throwf(c, CHEAX_ETYPE, "invalid type");
		return;
	}

	struct sync_nstring_info *info = sym->user_info;
	if (info->size - 1 < value.data.as_string->len) {
		cheax_throwf(c, CHEAX_EVALUE, "string too big");
		return;
	}

	memcpy(info->buf, value.data.as_string->value, value.data.as_string->len);
	info->buf[value.data.as_string->len] = '\0';
}
static void
sync_nstring_finalizer(CHEAX *c, struct chx_sym *sym)
{
	cheax_free(c, sym->user_info);
}

void
cheax_sync_nstring(CHEAX *c, const char *name, char *buf, size_t size, int flags)
{
	ASSERT_NOT_NULL_VOID("sync_nstring", buf);

	if (size == 0) {
		cheax_throwf(c, CHEAX_EAPI, "sync_nstring(): `size' cannot be zero");
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
	chx_ref get_ref, set_ref;
};

static void
defgetset(CHEAX *c, const char *name,
          struct chx_value getset_args,
          struct chx_list *args,
          struct defsym_info *info,
          struct chx_func **out,
          chx_ref *ref_out)
{
	if (info == NULL) {
		cheax_throwf(c, CHEAX_EEVAL, "out of symbol scope");
		return;
	}

	if (args == NULL) {
		cheax_throwf(c, CHEAX_EMATCH, "expected body");
		return;
	}

	if (*out != NULL) {
		cheax_throwf(c, CHEAX_EEXIST, "already called");
		return;
	}

	struct chx_func *res = gc_alloc(c, sizeof(struct chx_func), CHEAX_FUNC);
	if (res != NULL) {
		res->args = getset_args;
		res->body = args;
		res->lexenv = cheax_env(c).data.as_env;
		*ref_out = cheax_ref_ptr(c, res);
	}
	*out = res;
}

static struct chx_value
defsym_get(CHEAX *c, struct chx_sym *sym)
{
	struct defsym_info *info = sym->user_info;
	struct chx_list sexpr = { 0, cheax_func_value(info->get), NULL };
	struct chx_value sexpr_val;
	sexpr_val.type = CHEAX_LIST;
	sexpr_val.data.as_list = &sexpr;
	return cheax_eval(c, sexpr_val);
}
static void
defsym_set(CHEAX *c, struct chx_sym *sym, struct chx_value value)
{
	struct defsym_info *info = sym->user_info;
	cheax_apply(c, cheax_func_value(info->set), cheax_list(c, value, NULL).data.as_list);
}
static void
defsym_finalizer(CHEAX *c, struct chx_sym *sym)
{
	cheax_free(c, sym->user_info);
}

static void
eval_defsym_stat(CHEAX *c, struct chx_value stat, struct defsym_info *info)
{
	if (stat.type != CHEAX_LIST) {
		cheax_eval(c, stat);
		return;
	}

	struct chx_list *lst = stat.data.as_list;
	if (lst == NULL)
		return;

	struct chx_value head = lst->value;
	struct chx_list *tail = lst->next;

	if (head.type != CHEAX_ID) {
		cheax_eval(c, stat);
		return;
	}

	const char *id = head.data.as_id->value;

	if (0 == strcmp(id, "defget")) {
		defgetset(c, "defget", CHEAX_NIL, tail, info, &info->get, &info->get_ref);
		return;
	}

	if (0 == strcmp(id, "defset")) {
		struct chx_value id = cheax_id(c, "value");
		struct chx_value set_args = cheax_list(c, id, NULL);

		defgetset(c, "defset", set_args, tail, info, &info->set, &info->set_ref);
		return;
	}

	cheax_eval(c, stat);
}

static int
sf_defsym(CHEAX *c, struct chx_list *args, void *info, struct chx_env *ps, union chx_eval_out *out)
{
	if (args == NULL) {
		cheax_throwf(c, CHEAX_EMATCH, "expected symbol name");
		out->value = bt_wrap(c, CHEAX_NIL);
		return CHEAX_VALUE_OUT;
	}

	struct chx_value idval = args->value;
	if (idval.type != CHEAX_ID) {
		cheax_throwf(c, CHEAX_ETYPE, "expected identifier");
		out->value = bt_wrap(c, CHEAX_NIL);
		return CHEAX_VALUE_OUT;
	}

	struct chx_id *id = idval.data.as_id;
	bool body_ok = false;

	struct defsym_info *dinfo = cheax_malloc(c, sizeof(struct defsym_info));
	if (dinfo == NULL) {
		out->value = bt_wrap(c, CHEAX_NIL);
		return CHEAX_VALUE_OUT;
	}
	dinfo->get = dinfo->set = NULL;

	cheax_push_env(c);
	cheax_ft(c, pad);

	for (struct chx_list *cons = args->next; cons != NULL; cons = cons->next) {
		eval_defsym_stat(c, cons->value, dinfo);
		cheax_ft(c, pad);
	}

	body_ok = true;
pad:
	if (dinfo->get != NULL)
		cheax_unref_ptr(c, dinfo->get, dinfo->get_ref);
	if (dinfo->set != NULL)
		cheax_unref_ptr(c, dinfo->set, dinfo->set_ref);

	cheax_pop_env(c);

	if (!body_ok)
		goto err_pad;

	if (dinfo->get == NULL && dinfo->set == NULL) {
		cheax_throwf(c, CHEAX_ENOSYM, "symbol must have getter or setter");
		goto err_pad;
	}

	chx_getter act_get = (dinfo->get == NULL) ? NULL : defsym_get;
	chx_setter act_set = (dinfo->set == NULL) ? NULL : defsym_set;
	struct chx_sym *sym = cheax_defsym(c, id->value, act_get, act_set, defsym_finalizer, dinfo);
	if (sym == NULL)
		goto err_pad;

	struct chx_list *protect = NULL;
	if (dinfo->get != NULL)
		protect = cheax_list(c, cheax_func_value(dinfo->get), protect).data.as_list;
	if (dinfo->set != NULL)
		protect = cheax_list(c, cheax_func_value(dinfo->set), protect).data.as_list;
	sym->protect = cheax_list_value(protect);

	out->value = CHEAX_NIL;
	return CHEAX_VALUE_OUT;
err_pad:
	cheax_free(c, dinfo);
	out->value = bt_wrap(c, CHEAX_NIL);
	return CHEAX_VALUE_OUT;
}

static struct chx_value
pp_defsym_stat(CHEAX *c, struct chx_value stat)
{
	if (stat.type != CHEAX_LIST)
		return cheax_preproc(c, stat);

	struct chx_list *lst = stat.data.as_list;
	if (lst == NULL)
		return CHEAX_NIL;

	struct chx_value head = lst->value;

	if (head.type == CHEAX_ID
	 && (0 == strcmp(head.data.as_id->value, "defget")
	 ||  0 == strcmp(head.data.as_id->value, "defset")))
	{
		/* (node LIT (node EXPR (seq EXPR))) */
		static const uint8_t ops[] = {
			PP_NODE, PP_LIT, PP_NODE | PP_ERR(0), PP_EXPR, PP_SEQ, PP_EXPR,
		};
		static const char *errors[] = { "expected body" };
		return preproc_pattern(c, stat, ops, errors);
	}

	return cheax_preproc(c, stat);
}

static struct chx_value
pp_sf_defsym(CHEAX *c, struct chx_list *args, void *info)
{
	struct chx_list *out = NULL, **nextp = &out;

	if (args == NULL) {
		cheax_throwf(c, CHEAX_ESTATIC, "expected identifier");
		return CHEAX_NIL;
	}

	out = cheax_list(c, args->value, NULL).data.as_list;
	nextp = &out->next;
	cheax_ft(c, pad);

	for (args = args->next; args != NULL; args = args->next) {
		chx_ref ref = cheax_ref_ptr(c, out);
		struct chx_value stat = pp_defsym_stat(c, args->value);
		cheax_unref_ptr(c, out, ref);

		cheax_ft(c, pad);

		*nextp = cheax_list(c, stat, NULL).data.as_list;
		nextp = &(*nextp)->next;

		/* Allocation failure */
		cheax_ft(c, pad);
	}

	return cheax_list_value(out);
pad:
	return CHEAX_NIL;
}

static int
sf_def(CHEAX *c, struct chx_list *args, void *info, struct chx_env *ps, union chx_eval_out *out)
{
	int flags = (intptr_t)info;

	struct chx_value idval, setto;
	if (0 == unpack(c, args, has_flag(flags, CHEAX_READONLY) ? "_." : "_.?", &idval, &setto)
	 && !cheax_match(c, idval, setto, flags)
	 && cheax_errno(c) == 0)
	{
		cheax_throwf(c, CHEAX_EMATCH, "invalid pattern");
		cheax_add_bt(c);
	}
	out->value = CHEAX_NIL;
	return CHEAX_VALUE_OUT;
}

static struct chx_value
pp_sf_def(CHEAX *c, struct chx_list *args, void *info)
{
	/* (node LIT (node EXPR NIL)) */
	static const uint8_t ops[] = {
		PP_NODE | PP_ERR(0), PP_LIT, PP_NODE | PP_ERR(1), PP_EXPR, PP_NIL | PP_ERR(2),
	};

	static const char *errors[] = {
		"expected identifier",
		"expected value",
		"unexpected expression after value",
	};

	return preproc_pattern(c, cheax_list_value(args), ops, errors);
}

static struct chx_value
pp_sf_var(CHEAX *c, struct chx_list *args, void *info)
{
	/* (node LIT (maybe (node EXPR NIL))) */
	static const uint8_t ops[] = {
		PP_NODE | PP_ERR(0), PP_LIT, PP_MAYBE, PP_NODE, PP_EXPR, PP_NIL | PP_ERR(1),
	};

	static const char *errors[] = {
		"expected identifier",
		"unexpected expression after value",
	};

	return preproc_pattern(c, cheax_list_value(args), ops, errors);
}

static int
sf_set(CHEAX *c, struct chx_list *args, void *info, struct chx_env *ps, union chx_eval_out *out)
{
	const char *id;
	struct chx_value setto;
	if (unpack(c, args, "N!.", &id, &setto) < 0) {
		out->value = CHEAX_NIL;
		return CHEAX_VALUE_OUT;
	}

	cheax_set(c, id, setto);
	out->value = bt_wrap(c, CHEAX_NIL);
	return CHEAX_VALUE_OUT;
}

static int
sf_let(CHEAX *c,
       struct chx_list *args,
       void *info,
       struct chx_env *pop_stop,
       union chx_eval_out *out)
{
	struct chx_list *pairs, *body;
	if (unpack(c, args, "C_+", &pairs, &body) < 0) {
		out->value = CHEAX_NIL;
		return CHEAX_VALUE_OUT;
	}

	/* whether we're (let*) rather than (let) */
	bool star = (info != NULL);

	struct chx_env *outer_env = c->env;

	cheax_push_env(c);
	cheax_ft(c, pad2);

	struct chx_env *inner_env = c->env;
	chx_ref inner_env_ref = cheax_ref_ptr(c, inner_env);

	for (; pairs != NULL; pairs = pairs->next) {
		struct chx_value pairv = pairs->value;
		if (pairv.type != CHEAX_LIST) {
			cheax_throwf(c, CHEAX_ETYPE, "expected list of lists in first arg");
			cheax_add_bt(c);
			goto pad;
		}

		if (!star)
			c->env = outer_env;

		struct chx_value idval, setto;
		int upck = unpack(c, pairv.data.as_list, "_.", &idval, &setto);

		if (!star)
			c->env = inner_env;

		if (upck == 0
		 && !cheax_match(c, idval, setto, CHEAX_READONLY)
		 && cheax_errno(c) == 0)
		{
			cheax_throwf(c, CHEAX_EMATCH, "failed match in pair list");
			cheax_add_bt(c);
		}
		cheax_ft(c, pad);
	}

	if (body != NULL) {
		for (; body->next != NULL; body = body->next) {
			cheax_eval(c, body->value);
			cheax_ft(c, pad);
		}

		out->ts.tail = body->value;
		out->ts.pop_stop = pop_stop;
		return CHEAX_TAIL_OUT;
	}
pad:
	cheax_unref_ptr(c, inner_env, inner_env_ref);
	cheax_pop_env(c);
pad2:
	out->value = CHEAX_NIL;
	return CHEAX_VALUE_OUT;
}

static struct chx_value
pp_sf_let(CHEAX *c, struct chx_list *args, void *info)
{
	/* (node (seq (node LIT (node EXPR NIL))) (node EXPR (seq EXPR))) */
	static const uint8_t ops[] = {
		/* sequence of... */
		PP_NODE | PP_ERR(0), PP_SEQ | PP_ERR(1),
		/* ...pairs */
		PP_NODE | PP_ERR(2), PP_LIT, PP_NODE | PP_ERR(2), PP_EXPR, PP_NIL | PP_ERR(2),
		/* body */
		PP_NODE | PP_ERR(3), PP_EXPR, PP_SEQ, PP_EXPR,
	};

	static const char *errors[] = {
		"expected pair list",
		"expected list of pairs in second argument",
		"each let-pair must contain two values",
		"expected body",
	};

	return preproc_pattern(c, cheax_list_value(args), ops, errors);
}

static struct chx_value
bltn_env(CHEAX *c, struct chx_list *args, void *info)
{
	return (0 == unpack(c, args, ""))
	     ? cheax_env(c)
	     : CHEAX_NIL;
}

void
export_sym_bltns(CHEAX *c)
{
	cheax_defsyntax(c, "defsym",  sf_defsym, pp_sf_defsym, NULL);
	cheax_defsyntax(c, "var",     sf_def,    pp_sf_var,    (void *)0);
	cheax_defsyntax(c, "def",     sf_def,    pp_sf_def,    (void *)CHEAX_READONLY);
	cheax_defsyntax(c, "set",     sf_set,    pp_sf_def,    NULL);
	cheax_defsyntax(c, "let",     sf_let,    pp_sf_let,    NULL);
	cheax_defsyntax(c, "let*",    sf_let,    pp_sf_let,    (void *)1);
	cheax_defun(c, "env", bltn_env, NULL);
}
