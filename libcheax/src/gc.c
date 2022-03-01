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

#include "api.h"
#include "config.h"
#include "gc.h"
#include "rbtree.h"

#ifdef USE_BOEHM_GC

#include <gc/gc.h>

void
cheax_gc(CHEAX *c)
{
	/* empty */
}

void
cheax_force_gc(CHEAX *c)
{
	/* empty */
}

void
cheax_ref(CHEAX *c, void *obj)
{
	/* empty */
}

void
cheax_unref(CHEAX *c, void *obj)
{
	/* empty */
}

void
cheax_gc_init(CHEAX *c)
{
	/* empty */
}

void
cheax_gc_destroy(CHEAX *c)
{
	/* empty */
}

void
cheax_free(CHEAX *c, void *obj)
{
	GC_free(obj);
}

void *
cheax_alloc(CHEAX *c, size_t size, int type)
{
	struct chx_value *res = GC_malloc(size);
	res->type = type;
	return res;
}

void *
cheax_alloc_with_fin(CHEAX *c, size_t size, int type, chx_fin fin, void *info)
{
	struct chx_value *res = GC_malloc(size);
	res->type = type;
	GC_register_finalizer(res, fin, info, NULL, NULL);
	return res;
}


#else

struct gc_header {
	size_t size;    /* Total malloc()-ed size */
	int ext_refs;   /* Number of cheax_ref() references */

	int obj_start;  /* Only for locating the start of the user object */
};

struct gc_fin_footer {
	chx_fin fin;
	void *info;
};

static struct gc_header *
get_header(void *obj)
{
	char *m = obj;
	return (struct gc_header *)(m - offsetof(struct gc_header, obj_start));
}

static struct gc_fin_footer *
get_fin_footer(void *obj)
{
	struct gc_header *hdr = get_header(obj);
	char *m = (char *)hdr;
	size_t ftr_ofs = hdr->size - sizeof(struct gc_fin_footer);
	return (struct gc_fin_footer *)(m + ftr_ofs);
}

void
cheax_ref(CHEAX *c, void *value)
{
	if (value != NULL && gc_bits(value) != NO_GC_BIT)
		++get_header(value)->ext_refs;
}

void
cheax_unref(CHEAX *c, void *value)
{
	if (value != NULL && gc_bits(value) != NO_GC_BIT)
		--get_header(value)->ext_refs;
}

static int
obj_cmp(struct rb_tree *tree, struct rb_node *a, struct rb_node *b)
{
	ptrdiff_t d = (char *)a->value - (char *)b->value;
	return (d > 0) - (d < 0);
}

void
cheax_gc_init(CHEAX *c)
{
	rb_tree_init(&c->gc.all_objects, obj_cmp);
	c->gc.all_mem = c->gc.prev_run = 0;
	c->gc.free_all = c->gc.lock = false;
}

void
cheax_gc_destroy(CHEAX *c)
{
	c->gc.free_all = true;

	int attempts;
	for (attempts = 0; attempts < 3 && c->gc.all_objects.size > 0; ++attempts)
		cheax_force_gc(c);

	if (c->gc.all_objects.size > 0) {
		fprintf(stderr,
		        "cheax_destroy() warning: %d objects left after %d destruction attempts",
		        (int)c->gc.all_objects.size, attempts);
	}
}

void *
cheax_alloc(CHEAX *c, size_t size, int type)
{
	size_t total_size = size + sizeof(struct gc_header) - sizeof(int);
	struct gc_header *obj = malloc(total_size);
	if (obj == NULL) {
		cry(c, "cheax_alloc", CHEAX_ENOMEM, "out of memory");
		return NULL;
	}

	obj->size = total_size;
	obj->ext_refs = 0;
	c->gc.all_mem += total_size;
	struct chx_value *res = (struct chx_value *)&obj->obj_start;
	res->type = type;
	rb_tree_insert(&c->gc.all_objects, res);
	return res;
}

void *
cheax_alloc_with_fin(CHEAX *c, size_t size, int type, chx_fin fin, void *info)
{
	struct chx_value *obj = cheax_alloc(c, size + sizeof(struct gc_fin_footer), type);
	struct gc_fin_footer *ftr = get_fin_footer(obj);
	ftr->fin = fin;
	ftr->info = info;
	obj->type |= FIN_BIT;
	return obj;
}

void
cheax_free(CHEAX *c, void *obj)
{
	struct gc_header *header = get_header(obj);

	rb_tree_remove(&c->gc.all_objects, obj);

	if (has_fin_bit(obj)) {
		struct gc_fin_footer *ftr = get_fin_footer(obj);
		ftr->fin(obj, ftr->info);
	}

	c->gc.all_mem -= header->size;
	memset(header, 0, header->size);
	free(header);
}

struct gc_cycle {
	CHEAX *c;
	size_t num_in_use;

	size_t num_sweep;
	struct chx_value **sweep;
	int swept;
};

static void mark(struct gc_cycle *cycle, struct chx_value *used);

static void
mark_env(struct gc_cycle *cycle, struct rb_node *root)
{
	if (root == NULL)
		return;

	struct full_sym *sym = root->value;

	mark(cycle, sym->sym.protect);

	/* if it has a name (which it probably does), it's
	 * probably from a chx_id, which we need to push to
	 * the gray heap too */

	char *str_bytes = (char *)sym->name;
	void *id_bytes = str_bytes - sizeof(struct chx_id);
	struct chx_id *id = id_bytes;

	if (str_bytes != NULL
	 && rb_tree_find(&cycle->c->gc.all_objects, id_bytes) != NULL
	 && cheax_type_of(&id->base) == CHEAX_ID)
	{
		mark(cycle, &id->base);
	}

	for (int i = 0; i < 2; ++i)
		mark_env(cycle, root->link[i]);
}

static void
mark(struct gc_cycle *cycle, struct chx_value *used)
{
	if (used == NULL || gc_bits(used) != GC_UNMARKED)
		return;

	set_gc_bits(used, GC_MARKED);
	++cycle->num_in_use;

	struct chx_list *list;
	struct chx_func *func;
	struct chx_quote *quote;
	struct chx_env *env;
	switch (cheax_resolve_type(cycle->c, cheax_type_of(used))) {
	case CHEAX_LIST:
		list = (struct chx_list *)used;
		mark(cycle, list->value);
		mark(cycle, &list->next->base);
		break;

	case CHEAX_FUNC:
	case CHEAX_MACRO:
		func = (struct chx_func *)used;
		mark(cycle, func->args);
		mark(cycle, &func->body->base);
		mark(cycle, &func->lexenv->base);
		break;

	case CHEAX_QUOTE:
	case CHEAX_BACKQUOTE:
	case CHEAX_COMMA:
		quote = (struct chx_quote *)used;
		mark(cycle, quote->value);
		break;

	case CHEAX_ENV:
		env = (struct chx_env *)used;
		if (env->is_bif) {
			for (int i = 0; i < 2; ++i)
				mark(cycle, &env->value.bif[i]->base);
		} else {
			mark_env(cycle, env->value.norm.syms.root);
			mark(cycle, &env->value.norm.below->base);
		}
		break;
	}
}

static void
traverse_with(struct gc_cycle *cycle,
              struct rb_node *root,
              void (*fun)(struct gc_cycle *, struct chx_value *))
{
	if (root != NULL) {
		fun(cycle, root->value);
		for (int i = 0; i < 2; ++i)
			traverse_with(cycle, root->link[i], fun);
	}
}

void
cheax_gc(CHEAX *c)
{
	size_t mem = c->gc.all_mem;
	size_t prev = c->gc.prev_run;
	if (mem > prev && mem - prev >= GC_RUN_THRESHOLD)
		cheax_force_gc(c);
}

static void
mark_ext_refd(struct gc_cycle *cycle, struct chx_value *obj)
{
	if (get_header(obj)->ext_refs > 0)
		mark(cycle, obj);
}

static void
sweep(struct gc_cycle *cycle, struct chx_value *obj)
{
	if (gc_bits(obj) == GC_UNMARKED)
		cycle->sweep[cycle->swept++] = obj;
	else
		set_gc_bits(obj, GC_UNMARKED);
}

void
cheax_force_gc(CHEAX *c)
{
	if (c->gc.lock)
		return;

	c->gc.lock = true;

	struct gc_cycle cycle = { .c = c, 0 };

	if (!c->gc.free_all) {
		/* mark all cheax_ref()'d values in use */
		traverse_with(&cycle, c->gc.all_objects.root, mark_ext_refd);

		mark(&cycle, &c->env->base);
		mark_env(&cycle, c->globals.value.norm.syms.root);
		mark(&cycle, &c->error.msg->base);
	}

	/*
	 * We cannot simply traverse all objects and free anything not
	 * in use, since
	 *   1. cheax_free() messes with c->gc.all_objects, messing up
	 *      our traversal, and
	 *   2. a finalizer may allocate and mess with c->gc.all_objects.
	 * Thus we chuck all doomed objects into an array.
	 */
	cycle.num_sweep = c->gc.all_objects.size - cycle.num_in_use;
	cycle.sweep = calloc(cycle.num_sweep, sizeof(struct chx_value *));
	cycle.swept = 0;

	traverse_with(&cycle, c->gc.all_objects.root, sweep);

	for (size_t i = 0; i < cycle.num_sweep; ++i)
		cheax_free(c, cycle.sweep[i]);

	free(cycle.sweep);

	c->gc.prev_run = c->gc.all_mem;
	c->gc.lock = false;
}

#endif
