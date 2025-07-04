/* Copyright (c) 2024, Antonie Blom
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

#include "attrib.h"
#include "htab.h"
#include "setup.h"
#include "types.h"

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
#include <stdalign.h>
#include <stdlib.h>
#include <string.h>

#include "core.h"
#include "err.h"
#include "feat.h"
#include "gc.h"
#include "unpack.h"

struct gc_header {
	struct gc_header_node node;
	int rsvd_type;     /* Resolved cheax type of allocated value */
	union chx_any obj; /* Only for locating the start of the user object */
};

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
	alignas(max_align_t) char obj[1];
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

	return &ptr->obj[0];
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

void
cheax_gc_init_(CHEAX *c)
{
	c->gc.objects.first = c->gc.objects.last = &c->gc.objects;

	c->gc.all_mem = c->gc.prev_run = c->gc.num_objects = 0;
	c->gc.lock = c->gc.triggered = false;

	memset(c->gc.finalizers, 0, sizeof(c->gc.finalizers));
}

/* sets GC_MARKED bit for all reachable objects */
static void mark(CHEAX *c);
/* locks GC and cheax_gc_free_()'s all non-GC_MARKED objects */
static void sweep(CHEAX *c);

void
cheax_gc_cleanup_(CHEAX *c)
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
cheax_gc_alloc_(CHEAX *c, size_t size, int type)
{
	const size_t hdr_size = offsetof(struct gc_header, obj);
	if (size > SIZE_MAX - hdr_size) {
		cheax_throwf(c, CHEAX_ENOMEM, "cheax_gc_alloc_(): not enough space for gc header");
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
	hdr->obj.rtflags = GC_BIT;

	++c->gc.num_objects;

	/* insert new object */
	struct gc_header_node *new, *prev, *next;
	new = &hdr->node;
	prev = &c->gc.objects;
	next = c->gc.objects.first;

	new->prev = prev;
	new->next = next;
	next->prev = new;
	prev->next = new;

	return &hdr->obj;
}

void
cheax_gc_free_(CHEAX *c, void *obj)
{
	if (obj == NULL)
		return;

	struct gc_header *hdr = container_of(obj, struct gc_header, obj);

	struct gc_header_node *prev, *next;
	prev = hdr->node.prev;
	next = hdr->node.next;
	prev->next = next;
	next->prev = prev;
	--c->gc.num_objects;

	chx_fin fin = c->gc.finalizers[hdr->rsvd_type];
	if (fin != NULL)
		fin(c, obj);

	cheax_free(c, hdr);
}

void
cheax_gc_register_finalizer_(CHEAX *c, int type, chx_fin fin)
{
	c->gc.finalizers[type] = fin;
}

static bool
mark_once(CHEAX *c, void *obj)
{
	if (obj != NULL && (*(unsigned *)obj & (GC_BIT | GC_MARKED)) == GC_BIT) {
		*(unsigned *)obj |= GC_MARKED;
		return true;
	}
	return false;
}

static void mark_obj(CHEAX *c, struct chx_value used);

static void
mark_string(CHEAX *c, struct chx_string *str)
{
	for (; mark_once(c, str); str = str->orig)
		;
}
static void
mark_doc(struct htab_entry *entry, void *data)
{
	CHEAX *c = data;
	struct attrib *attrib = container_of(entry, struct attrib, entry);
	mark_string(c, attrib->doc);
}
static void
mark_list(CHEAX *c, struct chx_list *lst)
{
	/* Only list heads can have the orig_form attribute (right?) */
	struct attrib *orig_form_attr = cheax_attrib_get_(c, lst, ATTRIB_ORIG_FORM);
	if (orig_form_attr != NULL)
		mark_list(c, orig_form_attr->orig_form);

	for (; mark_once(c, lst); lst = lst->next)
		mark_obj(c, lst->value);
}
static void
mark_env_member(struct htab_entry *item, void *data)
{
	CHEAX *c = data;
	struct full_sym *sym = container_of(item, struct full_sym, entry);
	mark_obj(c, cheax_id_value(sym->name));
	mark_obj(c, sym->sym.protect);
	mark_string(c, sym->sym.doc);
}
static void
mark_env_members(CHEAX *c, struct htab *htab)
{
	cheax_htab_foreach_(htab, mark_env_member, c);
}
static void
mark_env(CHEAX *c, struct chx_env *env)
{
	while (mark_once(c, env)) {
		if (env->is_bif) {
			mark_env(c, env->value.bif[0]);
			env = env->value.bif[1];
		} else {
			mark_env_members(c, &env->value.norm.syms);
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
		mark_list(c, used.as_list);
		return;
	case CHEAX_STRING:
		mark_string(c, used.as_string);
		return;
	case CHEAX_ENV:
		mark_env(c, used.as_env);
		return;
	}

	if (!mark_once(c, used.rtflags_ptr))
		return;

	switch (ty) {
	case CHEAX_FUNC:
		mark_list(c, used.as_func->body);
		mark_env(c, used.as_func->lexenv);
		mark_obj(c, used.as_func->args);
		break;

	case CHEAX_QUOTE:
	case CHEAX_BACKQUOTE:
	case CHEAX_COMMA:
	case CHEAX_SPLICE:
		mark_obj(c, used.as_quote->value);
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
	for (n = c->gc.objects.first; n != &c->gc.objects; n = n->next) {
		struct gc_header *hdr = (struct gc_header *)n;
		if (has_flag(hdr->obj.rtflags, REF_BIT)) {
			mark_obj(c, ((struct chx_value){ .type     = hdr->rsvd_type,
			                                 .user_ptr = &hdr->obj }));
		}
	}

	mark_env(c, c->env);
	mark_env_members(c, &c->global_ns.value.norm.syms);
	mark_env_members(c, &c->specop_ns.value.norm.syms);
	mark_env_members(c, &c->macro_ns.value.norm.syms);
	mark_string(c, c->error.msg);

	for (int i = 0; i < NUM_STD_IDS; ++i)
		mark_obj(c, cheax_id_value(c->std_ids[i]));

	cheax_htab_foreach_(&c->attribs[ATTRIB_DOC].table, mark_doc, c);
}

static void
sweep(CHEAX *c)
{
	bool was_locked = c->gc.lock;
	c->gc.lock = true;

	struct gc_header_node *n, *nxt;
	for (n = c->gc.objects.first; n != &c->gc.objects; n = nxt) {
		nxt = n->next;
		struct gc_header *hdr = (struct gc_header *)n;
		if (!has_flag(hdr->obj.rtflags, GC_MARKED))
			cheax_gc_free_(c, &hdr->obj);
		else
			hdr->obj.rtflags &= ~GC_MARKED;
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
	return gc_type(c, value.type) ? cheax_ref_ptr(c, value.rtflags_ptr) : DO_NOTHING;
}

chx_ref
cheax_ref_ptr(CHEAX *c, void *restrict value)
{
	if (value != NULL && (*(unsigned *)value & (GC_BIT | REF_BIT)) == GC_BIT) {
		*(unsigned *)value |= REF_BIT;
		return PLEASE_UNREF;
	}

	return DO_NOTHING;
}

void
cheax_unref(CHEAX *c, struct chx_value value, chx_ref ref)
{
	cheax_unref_ptr(c, value.rtflags_ptr, ref);
}

void
cheax_unref_ptr(CHEAX *c, void *restrict value, chx_ref ref)
{
	if (ref == PLEASE_UNREF)
		*(unsigned *)value &= ~REF_BIT;
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
	if (cheax_unpack_(c, args, "") < 0)
		return CHEAX_NIL;

	int mem_i = c->gc.all_mem, obj_i = c->gc.num_objects;
	cheax_force_gc(c);
	int mem_f = c->gc.all_mem, obj_f = c->gc.num_objects;

	struct chx_value res[] = {
		cheax_id(c, "mem"), cheax_int(mem_i), cheax_id(c, "->"), cheax_int(mem_f),
		cheax_id(c, "obj"), cheax_int(obj_i), cheax_id(c, "->"), cheax_int(obj_f),
	};
	return cheax_bt_wrap_(c, cheax_array_to_list(c, res, sizeof(res) / sizeof(res[0])));
}

static struct chx_value
bltn_get_used_memory(CHEAX *c, struct chx_list *args, void *info)
{
	return (0 == cheax_unpack_(c, args, ""))
	     ? cheax_bt_wrap_(c, cheax_int(c->gc.all_mem))
	     : CHEAX_NIL;
}

void
cheax_load_gc_feature_(CHEAX *c, int bits)
{
	if (has_flag(bits, GC_BUILTIN)) {
		cheax_defun(c, "gc", bltn_gc, NULL);
		cheax_defun(c, "get-used-memory", bltn_get_used_memory, NULL);
	}
}
