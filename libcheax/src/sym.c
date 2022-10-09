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
	     ? cheax_nil()
	     : cheax_env_value(c->env);
}

void
cheax_push_env(CHEAX *c)
{
	struct chx_env *env = gc_alloc_with_fin(c,
	                                        sizeof(struct chx_env),
	                                        CHEAX_ENV,
	                                        norm_env_fin,
	                                        NULL);
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

	/* TODO re-implement with tail call elimination in mind */
	/* dangerous, but worth it! */
	/*if (env->rtflags & (NO_ESC_BIT | REF_BIT) == NO_ESC_BIT)
		gc_free(c, env);*/
}

struct chx_sym *
cheax_defsym(CHEAX *c, const char *id,
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
		env = &c->globals;

	struct full_sym *prev_fs = find_sym_in(env, id);
	if (prev_fs != NULL && !prev_fs->allow_redef) {
		cheax_throwf(c, CHEAX_EEXIST, "symbol `%s' already exists", id);
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
	fs->allow_redef = c->allow_redef && (env == &c->globals);
	fs->sym.get = get;
	fs->sym.set = set;
	fs->sym.fin = fin;
	fs->sym.user_info = user_info;
	fs->sym.protect = cheax_nil();

	if (prev_fs != NULL && prev_fs->allow_redef)
		undef_sym(c, env, prev_fs);

	rb_tree_insert(&env->value.norm.syms, fs);
	return &fs->sym;
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
cheax_def(CHEAX *c, const char *id, struct chx_value value, int flags)
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
cheax_defun(CHEAX *c, const char *id, chx_func_ptr perform, void *info)
{
	cheax_def(c, id, cheax_ext_func(c, id, perform, info), CHEAX_READONLY);
}
void
cheax_def_special_form(CHEAX *c, const char *id, chx_func_ptr perform, void *info)
{
	cheax_def(c, id, cheax_special_form(c, id, perform, info), CHEAX_READONLY);
}
void
cheax_def_special_tail_form(CHEAX *c, const char *id, chx_tail_func_ptr perform, void *info)
{
	cheax_def(c, id, cheax_special_tail_form(c, id, perform, info), CHEAX_READONLY);
}

void
cheax_set(CHEAX *c, const char *id, struct chx_value value)
{
	ASSERT_NOT_NULL_VOID("set", id);

	struct full_sym *fs = find_sym(c, id);
	if (fs == NULL) {
		cheax_throwf(c, CHEAX_ENOSYM, "no such symbol `%s'", id);
		return;
	}

	struct chx_sym *sym = &fs->sym;
	if (sym->set == NULL)
		cheax_throwf(c, CHEAX_EREADONLY, "cannot write to read-only symbol");
	else
		sym->set(c, sym, value);
}

struct chx_value
cheax_get(CHEAX *c, const char *id)
{
	ASSERT_NOT_NULL("get", id, cheax_nil());

	struct full_sym *fs = find_sym(c, id);
	if (fs == NULL) {
		cheax_throwf(c, CHEAX_ENOSYM, "no such symbol `%s'", id);
		return cheax_nil();
	}

	struct chx_sym *sym = &fs->sym;
	if (sym->get == NULL) {
		cheax_throwf(c, CHEAX_EWRITEONLY, "cannot read from write-only symbol");
		return cheax_nil();
	}

	return sym->get(c, sym);
}

struct chx_value
cheax_get_from(CHEAX *c, struct chx_env *env, const char *id)
{
	ASSERT_NOT_NULL("get_from", id, cheax_nil());

	struct full_sym *fs = find_sym_in(norm_env(env), id);
	if (fs == NULL) {
		cheax_throwf(c, CHEAX_ENOSYM, "no such symbol `%s'", id);
		return cheax_nil();
	}

	struct chx_sym *sym = &fs->sym;
	if (sym->get == NULL) {
		cheax_throwf(c, CHEAX_EWRITEONLY, "cannot read from write-only symbol");
		return cheax_nil();
	}

	return sym->get(c, sym);
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
};

static void
defgetset(CHEAX *c, const char *name,
          struct chx_value getset_args,
          struct chx_list *args,
          struct defsym_info *info,
          struct chx_func **out)
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
	}
	*out = res;
}

static struct chx_value
sf_defget(CHEAX *c, struct chx_list *args, void *info)
{
	struct defsym_info *dinfo = info;
	defgetset(c, "defget", cheax_nil(), args, dinfo, &dinfo->get);
	return bt_wrap(c, cheax_nil());
}
static struct chx_value
sf_defset(CHEAX *c, struct chx_list *args, void *info)
{
	static struct chx_id id = { 0, "value" };
	static struct chx_list set_args = { 0, { .type = CHEAX_ID, .data.as_id = &id }, NULL };
	static struct chx_value set_args_val = { .type = CHEAX_LIST, .data = { .as_list = &set_args } };

	struct defsym_info *dinfo = info;
	defgetset(c, "defset", set_args_val, args, dinfo, &dinfo->set);
	return bt_wrap(c, cheax_nil());
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

static struct chx_value
sf_defsym(CHEAX *c, struct chx_list *args, void *info)
{
	if (args == NULL) {
		cheax_throwf(c, CHEAX_EMATCH, "expected symbol name");
		return bt_wrap(c, cheax_nil());
	}

	struct chx_value idval = args->value;
	if (idval.type != CHEAX_ID) {
		cheax_throwf(c, CHEAX_ETYPE, "expected identifier");
		return bt_wrap(c, cheax_nil());
	}

	struct chx_id *id = idval.data.as_id;
	bool body_ok = false, add_bt = true;

	struct defsym_info *dinfo = cheax_malloc(c, sizeof(struct defsym_info));
	if (dinfo == NULL)
		return bt_wrap(c, cheax_nil());
	dinfo->get = dinfo->set = NULL;

	struct chx_value defget, defset;
	defget = cheax_special_form(c, "defget", sf_defget, dinfo);
	defset = cheax_special_form(c, "defset", sf_defset, dinfo);
	cheax_ft(c, err_pad); /* alloc failure */

	cheax_push_env(c);
	cheax_ft(c, err_pad);

	cheax_def(c, "defget", defget, CHEAX_READONLY);
	cheax_def(c, "defset", defset, CHEAX_READONLY);
	cheax_ft(c, pad); /* alloc failure */

	add_bt = false;
	for (struct chx_list *cons = args->next; cons != NULL; cons = cons->next) {
		cheax_eval(c, cons->value);
		cheax_ft(c, pad);
	}

	body_ok = add_bt = true;
pad:
	defget.data.as_form->info = defset.data.as_form->info = NULL;
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

	return cheax_nil();
err_pad:
	cheax_free(c, dinfo);
	return add_bt ? bt_wrap(c, cheax_nil()) : cheax_nil();
}

static struct chx_value
sf_def(CHEAX *c, struct chx_list *args, void *info)
{
	struct chx_value idval, setto;
	if (0 == unpack(c, args, "_.", &idval, &setto)
	 && !cheax_match(c, idval, setto, CHEAX_READONLY)
	 && cheax_errno(c) == 0)
	{
		cheax_throwf(c, CHEAX_EMATCH, "invalid pattern");
		cheax_add_bt(c);
	}
	return cheax_nil();
}
static struct chx_value
sf_var(CHEAX *c, struct chx_list *args, void *info)
{
	struct chx_value idval, setto;
	if (0 == unpack(c, args, "_.?", &idval, &setto)
	 && !cheax_match(c, idval, setto, 0)
	 && cheax_errno(c) == 0)
	{
		cheax_throwf(c, CHEAX_EMATCH, "invalid pattern");
		cheax_add_bt(c);
	}
	return cheax_nil();
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
		out->value = cheax_nil();
		return CHEAX_VALUE_OUT;
	}

	cheax_push_env(c);
	cheax_ft(c, pad2);

	for (; pairs != NULL; pairs = pairs->next) {
		struct chx_value pairv = pairs->value;
		if (pairv.type != CHEAX_LIST) {
			cheax_throwf(c, CHEAX_ETYPE, "expected list of lists in first arg");
			cheax_add_bt(c);
			goto pad;
		}

		struct chx_value idval, setto;
		if (0 == unpack(c, pairv.data.as_list, "_.", &idval, &setto)
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
	cheax_pop_env(c);
pad2:
	out->value = cheax_nil();
	return CHEAX_VALUE_OUT;
}

static struct chx_value
sf_set(CHEAX *c, struct chx_list *args, void *info)
{
	const char *id;
	struct chx_value setto;
	if (unpack(c, args, "N!.", &id, &setto) < 0)
		return cheax_nil();

	cheax_set(c, id, setto);
	return bt_wrap(c, cheax_nil());
}

static struct chx_value
sf_env(CHEAX *c, struct chx_list *args, void *info)
{
	return (0 == unpack(c, args, ""))
	     ? cheax_env(c)
	     : cheax_nil();
}

void
export_sym_bltns(CHEAX *c)
{
	cheax_def_special_form(c, "defsym",  sf_defsym, NULL);
	cheax_def_special_form(c, "var",     sf_var,    NULL);
	cheax_def_special_form(c, "def",     sf_def,    NULL);
	cheax_def_special_tail_form(c, "let", sf_let,    NULL);
	cheax_def_special_form(c, "set",     sf_set,    NULL);
	cheax_def_special_form(c, "env",     sf_env,    NULL);
}
