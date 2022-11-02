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

#include "setup.h"

#if defined(HAVE_MALLOC_USABLE_SIZE)
#  define MSIZE malloc_usable_size
#elif defined(HAVE_WINDOWS_MSIZE)
#  define MSIZE _msize
#endif

#ifdef MSIZE
#  ifdef HAVE_MALLOC_H
#    include <malloc.h>
#  endif
#  ifdef HAVE_MALLOC_NP_H
#    include <malloc_np.h>
#  endif
#endif
#include <stdlib.h>
#include <string.h>

#include "core.h"
#include "err.h"
#include "feat.h"
#include "gc.h"
#include "rbtree.h"
#include "unpack.h"

#define RTFLAGS(obj) (*(unsigned *)(obj))

/* values for chx_ref */
enum {
	DO_NOTHING,
	PLEASE_UNREF,
};

static bool
gc_type(CHEAX *c, int type)
{
	switch (cheax_resolve_type(c, type)) {
	case CHEAX_INT:
	case CHEAX_BOOL:
	case CHEAX_DOUBLE:
	case CHEAX_USER_PTR:
		return false;
	default:
		return true;
	}
}

#ifndef MSIZE
struct alloc_header {
	size_t size;
	long obj;
};

#  define HDR_SIZE (offsetof(struct alloc_header, obj))
typedef struct alloc_header *alloc_ptr;

static alloc_ptr
get_alloc_ptr(void *obj)
{
	return (alloc_ptr)((char *)obj - HDR_SIZE);
}

static size_t
obj_size(void *obj)
{
	return get_alloc_ptr(obj)->size;
}

static void *
claim_mem(CHEAX *c, alloc_ptr ptr, size_t total_size, size_t unclaim)
{
	ptr->size = total_size;
	c->gc.all_mem = c->gc.all_mem - unclaim + total_size;

	size_t mem = c->gc.all_mem, prev_mem = c->gc.prev_run;
	c->gc.triggered = c->gc.triggered
	               || (mem > prev_mem && mem - prev_mem >= GC_RUN_THRESHOLD)
	               || (c->mem_limit > 256 && mem > (size_t)(c->mem_limit - 256));

	return &ptr->obj;
}

#else
#  define HDR_SIZE 0
typedef void *alloc_ptr;

static alloc_ptr
get_alloc_ptr(void *ptr)
{
	return ptr;
}

static size_t
obj_size(void *obj)
{
	return MSIZE(obj);
}

static void *
claim_mem(CHEAX *c, alloc_ptr ptr, size_t total_size, size_t unclaim)
{
	c->gc.all_mem = c->gc.all_mem - unclaim + MSIZE(ptr);

	size_t mem = c->gc.all_mem, prev_mem = c->gc.prev_run;
	c->gc.triggered = c->gc.triggered
	               || (mem > prev_mem && mem - prev_mem >= GC_RUN_THRESHOLD)
	               || (c->mem_limit > 256 && mem > (size_t)(c->mem_limit - 256));

	return ptr;
}
#endif

static int
check_mem(CHEAX *c, size_t size)
{
#if HDR_SIZE > 0
	if (size > SIZE_MAX - HDR_SIZE) {
		cheax_throwf(c, CHEAX_ENOMEM, "check_mem(): not enough space for alloc header");
		return -1;
	}

	size += HDR_SIZE;
#endif
	size_t limit = c->mem_limit;
	if (c->gc.all_mem > SIZE_MAX - size || (c->mem_limit > 0 && c->gc.all_mem + size > limit)) {
		cheax_throwf(c, CHEAX_ENOMEM, "check_mem(): memory limit reached (%zd bytes)", limit);
		return -1;
	}

	return 0;
}

void *
cheax_malloc(CHEAX *c, size_t size)
{
	if (size == 0 || check_mem(c, size) < 0)
		return NULL;

	alloc_ptr ptr = malloc(size + HDR_SIZE);
	if (ptr == NULL) {
		cheax_throwf(c, CHEAX_ENOMEM, "malloc() failure");
		return NULL;
	}

	return claim_mem(c, ptr, size, 0);
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

	if (check_mem(c, nmemb * size) < 0)
		return NULL;

	alloc_ptr ptr = malloc(nmemb * size + HDR_SIZE);
	if (ptr == NULL) {
		cheax_throwf(c, CHEAX_ENOMEM, "malloc() failure");
		return NULL;
	}

	return memset(claim_mem(c, ptr, nmemb * size + HDR_SIZE, 0), 0, nmemb * size);
}

void *
cheax_realloc(CHEAX *c, void *obj, size_t size)
{
	if (obj == NULL)
		return cheax_malloc(c, size);

	if (size == 0) {
		cheax_free(c, obj);
		return NULL;
	}

	alloc_ptr ptr = get_alloc_ptr(obj);
	size_t prev_size = obj_size(obj);

	c->gc.all_mem -= prev_size;
	int cmem = check_mem(c, size);
	c->gc.all_mem += prev_size;

	if (cmem < 0)
		return NULL;

	ptr = realloc(ptr, size + HDR_SIZE);
	if (ptr == NULL) {
		cheax_throwf(c, CHEAX_ENOMEM, "realloc() failure");
		return NULL;
	}

	return claim_mem(c, ptr, size + HDR_SIZE, prev_size);
}

void
cheax_free(CHEAX *c, void *obj)
{
	if (obj != NULL) {
		c->gc.all_mem -= obj_size(obj);
		free(get_alloc_ptr(obj));
	}
}

struct gc_fin_footer {
	chx_fin fin;
	void *info;
};

static struct gc_header *
get_header(void *obj)
{
	return (struct gc_header *)((char *)obj - offsetof(struct gc_header, obj));
}

static struct gc_fin_footer *
get_fin_footer(void *obj)
{
	struct gc_header *hdr = get_header(obj);
	char *mem_ptr = (char *)get_alloc_ptr(hdr);
	return (struct gc_fin_footer *)(mem_ptr + obj_size(hdr) - sizeof(struct gc_fin_footer));
}

void
gc_init(CHEAX *c)
{
	c->gc.objects.prev = c->gc.objects.next = &c->gc.objects;

	c->gc.all_mem = c->gc.prev_run = c->gc.num_objects = 0;
	c->gc.lock = c->gc.triggered = false;
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
	const size_t hdr_size = offsetof(struct gc_header, obj);
	if (size > SIZE_MAX - hdr_size) {
		cheax_throwf(c, CHEAX_ENOMEM, "gc_alloc(): not enough space for gc header");
		return NULL;
	}

	size_t total_size = size + hdr_size;
	struct gc_header *hdr = cheax_malloc(c, total_size);
	if (hdr == NULL)
		return NULL;

	int rsvd_type = cheax_resolve_type(c, type);
	if (rsvd_type < 0)
		return NULL;
	hdr->rsvd_type = rsvd_type;

	RTFLAGS(&hdr->obj) = GC_BIT;

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
	void *obj = gc_alloc(c, size + sizeof(struct gc_fin_footer), type);
	if (obj != NULL) {
		struct gc_fin_footer *ftr = get_fin_footer(obj);
		ftr->fin = fin;
		ftr->info = info;
		RTFLAGS(obj) |= FIN_BIT;
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

	if (has_flag(RTFLAGS(obj), FIN_BIT)) {
		struct gc_fin_footer *ftr = get_fin_footer(obj);
		ftr->fin(obj, ftr->info);
	}

	cheax_free(c, hdr);
}

static bool
mark_once(CHEAX *c, void *obj)
{
	if (obj != NULL && (RTFLAGS(obj) & (GC_BIT | GC_MARKED)) == GC_BIT) {
		RTFLAGS(obj) |= GC_MARKED;
		return true;
	}
	return false;
}

static void mark_obj(CHEAX *c, struct chx_value used);

static void
mark_list(CHEAX *c, struct chx_list *lst)
{
	struct chx_list *orig_form = get_orig_form(lst);
	if (orig_form != NULL)
		mark_list(c, orig_form);

	for (; mark_once(c, lst); lst = lst->next)
		mark_obj(c, lst->value);
}
static void
mark_string(CHEAX *c, struct chx_string *str)
{
	for (; mark_once(c, str); str = str->orig)
		;
}
static void
mark_env_members(CHEAX *c, struct rb_node *root)
{
	while (root != NULL) {
		struct full_sym *sym = root->value;
		mark_obj(c, sym->sym.protect);

		mark_env_members(c, root->link[0]);
		root = root->link[1];
	}
}
static void
mark_env(CHEAX *c, struct chx_env *env)
{
	while (mark_once(c, env)) {
		if (env->is_bif) {
			mark_env(c, env->value.bif[0]);
			env = env->value.bif[1];
		} else {
			mark_env_members(c, env->value.norm.syms.root);
			env = env->value.norm.below;
		}
	}
}

static void
mark_obj(CHEAX *c, struct chx_value used)
{
	int ty = cheax_resolve_type(c, used.type);
	if (!gc_type(c, ty))
		return;

	switch (ty) {
	case CHEAX_LIST:
		mark_list(c, used.data.as_list);
		return;
	case CHEAX_STRING:
		mark_string(c, used.data.as_string);
		return;
	case CHEAX_ENV:
		mark_env(c, used.data.as_env);
		return;
	}

	if (!mark_once(c, used.data.rtflags_ptr))
		return;

	switch (ty) {
	case CHEAX_FUNC:
		mark_list(c, used.data.as_func->body);
		mark_env(c, used.data.as_func->lexenv);
		mark_obj(c, used.data.as_func->args);
		break;

	case CHEAX_QUOTE:
	case CHEAX_BACKQUOTE:
	case CHEAX_COMMA:
	case CHEAX_SPLICE:
		mark_obj(c, used.data.as_quote->value);
		break;
	}
}

void
cheax_gc(CHEAX *c)
{
	if (c->gc.triggered || c->hyper_gc)
		cheax_force_gc(c);
}

static void
mark(CHEAX *c)
{
	struct gc_header_node *n;
	for (n = c->gc.objects.next; n != &c->gc.objects; n = n->next) {
		struct gc_header *hdr = (struct gc_header *)n;
		void *obj = &hdr->obj;
		if (has_flag(RTFLAGS(obj), REF_BIT))
			mark_obj(c, ((struct chx_value){ .type = hdr->rsvd_type, .data.user_ptr = obj }));
	}

	mark_env(c, c->env);
	mark_env_members(c, c->global_ns.value.norm.syms.root);
	mark_env_members(c, c->specop_ns.value.norm.syms.root);
	mark_env_members(c, c->macro_ns.value.norm.syms.root);
	mark_string(c, c->error.msg);
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
		void *obj = &hdr->obj;
		if (!has_flag(RTFLAGS(obj), GC_MARKED))
			gc_free(c, obj);
		else
			RTFLAGS(obj) &= ~GC_MARKED;
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
	c->gc.lock = c->gc.triggered = false;
}

chx_ref
cheax_ref(CHEAX *c, struct chx_value value)
{
	return gc_type(c, value.type) ? cheax_ref_ptr(c, value.data.rtflags_ptr) : DO_NOTHING;
}

chx_ref
cheax_ref_ptr(CHEAX *c, void *restrict value)
{
	if (value != NULL && (RTFLAGS(value) & (GC_BIT | REF_BIT)) == GC_BIT) {
		RTFLAGS(value) |= REF_BIT;
		return PLEASE_UNREF;
	}

	return DO_NOTHING;
}

void
cheax_unref(CHEAX *c, struct chx_value value, chx_ref ref)
{
	cheax_unref_ptr(c, value.data.rtflags_ptr, ref);
}

void
cheax_unref_ptr(CHEAX *c, void *restrict value, chx_ref ref)
{
	if (ref == PLEASE_UNREF)
		RTFLAGS(value) &= ~REF_BIT;
}

/*
 *  _           _ _ _   _
 * | |__  _   _(_) | |_(_)_ __  ___
 * | '_ \| | | | | | __| | '_ \/ __|
 * | |_) | |_| | | | |_| | | | \__ \
 * |_.__/ \__,_|_|_|\__|_|_| |_|___/
 *
 */

static struct chx_value
bltn_gc(CHEAX *c, struct chx_list *args, void *info)
{
	if (unpack(c, args, "") < 0)
		return cheax_nil();

	static struct chx_id mem = { 0, "mem" }, to = { 0, "->" }, obj = { 0, "obj" };

	int mem_i = c->gc.all_mem, obj_i = c->gc.num_objects;
	cheax_force_gc(c);
	int mem_f = c->gc.all_mem, obj_f = c->gc.num_objects;

	struct chx_value res[] = {
		{ .type = CHEAX_ID, .data = { .as_id = &mem } },
		cheax_int(mem_i),
		{ .type = CHEAX_ID, .data = { .as_id = &to  } },
		cheax_int(mem_f),

		{ .type = CHEAX_ID, .data = { .as_id = &obj } },
		cheax_int(obj_i),
		{ .type = CHEAX_ID, .data = { .as_id = &to  } },
		cheax_int(obj_f),
	};
	return bt_wrap(c, cheax_array_to_list(c, res, sizeof(res) / sizeof(res[0])));
}

static struct chx_value
bltn_get_used_memory(CHEAX *c, struct chx_list *args, void *info)
{
	return (0 == unpack(c, args, ""))
	     ? bt_wrap(c, cheax_int(c->gc.all_mem))
	     : cheax_nil();
}

void
load_gc_feature(CHEAX *c, int bits)
{
	if (has_flag(bits, GC_BUILTIN)) {
		cheax_defun(c, "gc", bltn_gc, NULL);
		cheax_defun(c, "get-used-memory", bltn_get_used_memory, NULL);
	}
}
