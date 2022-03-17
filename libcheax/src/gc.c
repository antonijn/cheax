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

#include "core.h"
#include "err.h"
#include "feat.h"
#include "gc.h"
#include "rbtree.h"
#include "setup.h"
#include "unpack.h"

/* values for chx_ref */
enum {
	DO_NOTHING,
	PLEASE_UNREF,
};

struct alloc_header {
	size_t size;
	long obj;
};

static struct alloc_header *
get_alloc_header(void *ptr)
{
	char *m = ptr;
	return (struct alloc_header *)(m - offsetof(struct alloc_header, obj));
}

static int
claim_mem(CHEAX *c, size_t size, size_t hdr_size)
{
	if (size > SIZE_MAX - hdr_size) {
		cheax_throwf(c, CHEAX_ENOMEM, "claim_mem(): not enough space for alloc header");
		return -1;
	}

	size += hdr_size;

	size_t limit = c->mem_limit;
	if (c->gc.all_mem > SIZE_MAX - size
	 || (c->mem_limit > 0 && c->gc.all_mem + size > limit))
	{
		cheax_throwf(c, CHEAX_ENOMEM, "claim_mem(): memory limit reached (%zd bytes)", limit);
		return -1;
	}

	c->gc.all_mem += size;
	return 0;
}

void *
cheax_malloc(CHEAX *c, size_t size)
{
	struct alloc_header *hdr;
	const size_t hdr_size = sizeof(*hdr) - sizeof(hdr->obj);
	if (size == 0 || claim_mem(c, size, hdr_size) < 0)
		return NULL;

	hdr = malloc(size + hdr_size);
	if (hdr == NULL) {
		cheax_throwf(c, CHEAX_ENOMEM, "malloc() failure");
		return NULL;
	}

	hdr->size = size + hdr_size;
	return &hdr->obj;
}

void *
cheax_calloc(CHEAX *c, size_t nmemb, size_t size)
{
	if (size == 0 || nmemb == 0)
		return NULL;

	if (nmemb > SIZE_MAX / size) {
		cheax_throwf(c, CHEAX_ENOMEM, "calloc(): nmemb * size overflow");
		return NULL;
	}

	struct alloc_header *hdr;
	const size_t hdr_size = sizeof(*hdr) - sizeof(hdr->obj);
	if (claim_mem(c, nmemb * size, hdr_size) < 0)
		return NULL;

	hdr = malloc(nmemb * size + hdr_size);
	if (hdr == NULL) {
		cheax_throwf(c, CHEAX_ENOMEM, "malloc() failure");
		return NULL;
	}

	hdr->size = size + hdr_size;
	return memset(&hdr->obj, 0, size);
}

void *
cheax_realloc(CHEAX *c, void *ptr, size_t size)
{
	if (size == 0) {
		if (ptr != NULL)
			free(ptr);
		return NULL;
	}

	if (ptr == NULL)
		return cheax_malloc(c, size);

	struct alloc_header *hdr = get_alloc_header(ptr);
	const size_t hdr_size = sizeof(*hdr) - sizeof(hdr->obj);
	size_t prev_size = hdr->size;
	c->gc.all_mem -= prev_size;

	if (claim_mem(c, size, hdr_size) < 0) {
		c->gc.all_mem += prev_size;
		return NULL;
	}

	hdr = realloc(hdr, size + hdr_size);
	if (hdr == NULL) {
		cheax_throwf(c, CHEAX_ENOMEM, "realloc() failure");
		return NULL;
	}

	hdr->size = size + hdr_size;
	return &hdr->obj;
}

void
cheax_free(CHEAX *c, void *ptr)
{
	if (ptr != NULL) {
		struct alloc_header *hdr = get_alloc_header(ptr);
		c->gc.all_mem -= hdr->size;
		free(hdr);
	}
}

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
	struct alloc_header *hdr = get_alloc_header(get_header(obj));
	char *m = (char *)hdr;
	size_t ftr_ofs = hdr->size - sizeof(struct gc_fin_footer);
	return (struct gc_fin_footer *)(m + ftr_ofs);
}

void
gc_init(CHEAX *c)
{
	c->gc.objects.prev = c->gc.objects.next = &c->gc.objects;

	c->gc.all_mem = c->gc.prev_run = c->gc.num_objects = 0;
	c->gc.lock = false;
}

/* sets GC_MARKED bit for all reachable objects */
static void mark(CHEAX *c);
/* locks GC and gc_free()'s all non-GC_MARKED objects */
static void sweep(CHEAX *c);

void
gc_cleanup(CHEAX *c)
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
gc_alloc(CHEAX *c, size_t size, int type)
{
	struct gc_header *hdr;
	const size_t hdr_size = sizeof(*hdr) - sizeof(hdr->obj);
	if (size > SIZE_MAX - hdr_size) {
		cheax_throwf(c, CHEAX_ENOMEM, "gc_alloc(): not enough space for gc header");
		return NULL;
	}

	size_t total_size = size + hdr_size;
	hdr = cheax_malloc(c, total_size);
	if (hdr == NULL)
		return NULL;

	hdr->obj.type = type;
	hdr->obj.rtflags = GC_BIT;

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
gc_alloc_with_fin(CHEAX *c, size_t size, int type, chx_fin fin, void *info)
{
	struct chx_value *obj = gc_alloc(c, size + sizeof(struct gc_fin_footer), type);
	if (obj != NULL) {
		struct gc_fin_footer *ftr = get_fin_footer(obj);
		ftr->fin = fin;
		ftr->info = info;
		obj->rtflags |= FIN_BIT;
	}
	return obj;
}

void
gc_free(CHEAX *c, void *obj)
{
	if (obj == NULL)
		return;

	struct gc_header *hdr = get_header(obj);

	struct gc_header_node *prev, *next;
	prev = hdr->node.prev;
	next = hdr->node.next;
	prev->next = next;
	next->prev = prev;
	--c->gc.num_objects;

	if (has_flag(hdr->obj.rtflags, FIN_BIT)) {
		struct gc_fin_footer *ftr = get_fin_footer(obj);
		ftr->fin(obj, ftr->info);
	}

	cheax_free(c, hdr);
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
	if (used == NULL || (used->rtflags & (GC_BIT | GC_MARKED)) != GC_BIT)
		return;

	used->rtflags |= GC_MARKED;

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
	case CHEAX_SPLICE:
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
	size_t mem = c->gc.all_mem, prev = c->gc.prev_run;
	if ((mem > prev && mem - prev >= GC_RUN_THRESHOLD)
	 || (c->mem_limit > 256 && mem > (size_t)(c->mem_limit - 256)))
	{
		cheax_force_gc(c);
	}
}

static void
mark(CHEAX *c)
{
	struct gc_header_node *n;
	for (n = c->gc.objects.next; n != &c->gc.objects; n = n->next) {
		struct gc_header *hdr = (struct gc_header *)n;
		struct chx_value *obj = &hdr->obj;
		if (has_flag(obj->rtflags, REF_BIT))
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
		if (!has_flag(obj->rtflags, GC_MARKED))
			gc_free(c, obj);
		else
			obj->rtflags &= ~GC_MARKED;
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

chx_ref
cheax_ref(CHEAX *c, void *restrict value)
{
	struct chx_value *obj = value;
	if (obj != NULL && (obj->rtflags & (GC_BIT | REF_BIT)) == GC_BIT) {
		obj->rtflags |= REF_BIT;
		return PLEASE_UNREF;
	}
	return DO_NOTHING;
}

void
cheax_unref(CHEAX *c, void *restrict value, chx_ref ref)
{
	if (ref == PLEASE_UNREF) {
		struct chx_value *obj = value;
		obj->rtflags &= ~REF_BIT;
	}
}

/*
 *  _           _ _ _   _
 * | |__  _   _(_) | |_(_)_ __  ___
 * | '_ \| | | | | | __| | '_ \/ __|
 * | |_) | |_| | | | |_| | | | \__ \
 * |_.__/ \__,_|_|_|\__|_|_| |_|___/
 *
 */

static struct chx_value *
bltn_gc(CHEAX *c, struct chx_list *args, void *info)
{
	if (unpack(c, args, "") < 0)
		return NULL;

	static struct chx_id mem = { { CHEAX_ID, 0 }, "mem" },
			      to = { { CHEAX_ID, 0 }, "->" },
			     obj = { { CHEAX_ID, 0 }, "obj" };

	int mem_i = c->gc.all_mem, obj_i = c->gc.num_objects;
	cheax_force_gc(c);
	int mem_f = c->gc.all_mem, obj_f = c->gc.num_objects;

	struct chx_value *res[] = {
		&mem.base, &cheax_int(c, mem_i)->base, &to.base, &cheax_int(c, mem_f)->base,
		&obj.base, &cheax_int(c, obj_i)->base, &to.base, &cheax_int(c, obj_f)->base,
	};
	return bt_wrap(c, &cheax_array_to_list(c, res, sizeof(res) / sizeof(res[0]))->base);
}

static struct chx_value *
bltn_get_used_memory(CHEAX *c, struct chx_list *args, void *info)
{
	return (0 == unpack(c, args, ""))
	     ? bt_wrap(c, &cheax_int(c, c->gc.all_mem)->base)
	     : NULL;
}

void
load_gc_feature(CHEAX *c, int bits)
{
	if (has_flag(bits, GC_BUILTIN)) {
		cheax_defmacro(c, "gc", bltn_gc, NULL);
		cheax_defmacro(c, "get-used-memory", bltn_get_used_memory, NULL);
	}
}
