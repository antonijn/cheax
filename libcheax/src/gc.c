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
#include "setup.h"
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
gcol_init(CHEAX *c)
{
	/* empty */
}

void
gcol_destroy(CHEAX *c)
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

struct gc_fin_footer {
	chx_fin fin;
	void *info;
};

static struct gc_header *
get_header(void *obj)
{
	char *m = obj;
	return (struct gc_header *)(m - offsetof(struct gc_header, obj));
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
gcol_init(CHEAX *c)
{
	c->gc.objects.prev = c->gc.objects.next = &c->gc.objects;

	c->gc.all_mem = c->gc.prev_run = c->gc.num_objects = 0;
	c->gc.lock = false;
}

/* sets GC_MARKED bit for all reachable objects */
static void mark(CHEAX *c);
/* locks GC and cheax_free()'s all non-GC_MARKED objects */
static void sweep(CHEAX *c);

void
gcol_destroy(CHEAX *c)
{
	if (c->gc.lock) {
		/* give up*/
		fprintf(stderr, "cheax_destroy() warning: called from finalizer\n");
		return;
	}

	int attempts;
	for (attempts = 0; attempts < 3 && c->gc.num_objects > 0; ++attempts)
		sweep(c);

	if (c->gc.num_objects > 0) {
		fprintf(stderr,
		        "cheax_destroy() warning: %zu objects left after %d destruction attempts\n",
		        c->gc.num_objects, attempts);
	}
}

void *
cheax_alloc(CHEAX *c, size_t size, int type)
{
	struct gc_header *hdr;
	size_t total_size = size + sizeof(struct gc_header) - sizeof(hdr->obj);
	hdr = malloc(total_size);

	if (hdr == NULL) {
		cry(c, "cheax_alloc", CHEAX_ENOMEM, "out of memory");
		return NULL;
	}

	hdr->size = total_size;
	hdr->obj.type = type & CHEAX_TYPE_MASK;

	c->gc.all_mem += total_size;
	++c->gc.num_objects;

	/* insert new object */
	struct gc_header_node *new, *prev, *next;
	new = &hdr->node;
	prev = &c->gc.objects;
	next = c->gc.objects.next;

	new->prev = prev;
	new->next = next;
	next->prev = new;
	prev->next = new;

	return &hdr->obj;
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
	struct gc_header *hdr = get_header(obj);

	struct gc_header_node *prev, *next;
	prev = hdr->node.prev;
	next = hdr->node.next;
	prev->next = next;
	next->prev = prev;
	--c->gc.num_objects;

	if (has_fin_bit(obj)) {
		struct gc_fin_footer *ftr = get_fin_footer(obj);
		ftr->fin(obj, ftr->info);
	}

	c->gc.all_mem -= hdr->size;
	memset(hdr, 0, hdr->size);
	free(hdr);
}

static void mark_obj(CHEAX *c, struct chx_value *used);

static void
mark_env(CHEAX *c, struct rb_node *root)
{
	if (root == NULL)
		return;

	struct full_sym *sym = root->value;

	mark_obj(c, sym->sym.protect);

	for (int i = 0; i < 2; ++i)
		mark_env(c, root->link[i]);
}

static void
mark_obj(CHEAX *c, struct chx_value *used)
{
	if (used == NULL || has_flag(used->type, NO_GC_BIT) || has_flag(used->type, GC_MARKED))
		return;

	used->type |= GC_MARKED;

	struct chx_list *list;
	struct chx_string *str;
	struct chx_func *func;
	struct chx_quote *quote;
	struct chx_env *env;
	switch (cheax_resolve_type(c, cheax_type_of(used))) {
	case CHEAX_LIST:
		list = (struct chx_list *)used;
		mark_obj(c, list->value);
		mark_obj(c, &list->next->base);
		break;

	case CHEAX_STRING:
		str = (struct chx_string *)used;
		mark_obj(c, &str->orig->base);
		break;

	case CHEAX_FUNC:
	case CHEAX_MACRO:
		func = (struct chx_func *)used;
		mark_obj(c, func->args);
		mark_obj(c, &func->body->base);
		mark_obj(c, &func->lexenv->base);
		break;

	case CHEAX_QUOTE:
	case CHEAX_BACKQUOTE:
	case CHEAX_COMMA:
		quote = (struct chx_quote *)used;
		mark_obj(c, quote->value);
		break;

	case CHEAX_ENV:
		env = (struct chx_env *)used;
		if (env->is_bif) {
			for (int i = 0; i < 2; ++i)
				mark_obj(c, &env->value.bif[i]->base);
		} else {
			mark_env(c, env->value.norm.syms.root);
			mark_obj(c, &env->value.norm.below->base);
		}
		break;
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
mark(CHEAX *c)
{
	struct gc_header_node *n;
	for (n = c->gc.objects.next; n != &c->gc.objects; n = n->next) {
		struct gc_header *hdr = (struct gc_header *)n;
		struct chx_value *obj = &hdr->obj;
		if (has_flag(obj->type, GC_REFD))
			mark_obj(c, &hdr->obj);
	}

	mark_obj(c, &c->env->base);
	mark_env(c, c->globals.value.norm.syms.root);
	mark_obj(c, &c->error.msg->base);
}

static void
sweep(CHEAX *c)
{
	bool was_locked = c->gc.lock;
	c->gc.lock = true;

	struct gc_header_node *n, *nxt;
	for (n = c->gc.objects.next; n != &c->gc.objects; n = nxt) {
		nxt = n->next;
		struct gc_header *hdr = (struct gc_header *)n;
		struct chx_value *obj = &hdr->obj;
		if (!has_flag(obj->type, GC_MARKED))
			cheax_free(c, obj);
		else
			obj->type &= ~GC_MARKED;
	}

	c->gc.lock = was_locked;
}

void
cheax_force_gc(CHEAX *c)
{
	if (c->gc.lock)
		return;

	c->gc.lock = true;

	mark(c);
	sweep(c);

	c->gc.prev_run = c->gc.all_mem;
	c->gc.lock = false;
}

#endif

chx_ref
cheax_ref(CHEAX *c, void *restrict value)
{
	struct chx_value *obj = value;
	if (obj != NULL) {
		chx_ref res = has_flag(obj->type, GC_REFD);
		if (!has_flag(obj->type, NO_GC_BIT))
			obj->type |= GC_REFD;
		return res;
	}
	return false;
}

void
cheax_unref(CHEAX *c, void *restrict value, chx_ref ref)
{
	struct chx_value *obj = value;
	if (obj != NULL && !has_flag(obj->type, NO_GC_BIT) && !ref)
		obj->type &= ~GC_REFD;
}
