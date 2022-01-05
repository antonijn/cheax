/* Copyright (c) 2021, Antonie Blom
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

#include "api.h"
#include "gc.h"
#include "rbtree.h"

struct gc_header {
	size_t size;    /* Total malloc()-ed size */
	int ext_refs;   /* Number of cheax_ref() references */
	bool is_var;    /* Whether object is variable or a chx_value */

	int obj_start;  /* Only for locating the start of the user object */
};

static struct gc_header *
get_header(void *obj)
{
	char *m = obj;
	return (struct gc_header *)(m - offsetof(struct gc_header, obj_start));
}

void
cheax_ref(CHEAX *c, void *value)
{
	if (value != NULL)
		++get_header(value)->ext_refs;
}

void
cheax_unref(CHEAX *c, void *value)
{
	if (value != NULL)
		--get_header(value)->ext_refs;
}

static int
obj_cmp(struct rb_tree *tree, struct rb_node *a, struct rb_node *b)
{
	ptrdiff_t d = a->value - b->value;
	if (d < 0)
		return -1;
	if (d > 0)
		return 1;

	return 0;
}

void
cheax_gc_init(CHEAX *c)
{
	rb_tree_init(&c->gc.all_objects, obj_cmp);
	c->gc.all_mem = c->gc.prev_run = 0;
}

static void *
cheax_simple_alloc(CHEAX *c, size_t size)
{
	size_t total_size = size + sizeof(struct gc_header) - sizeof(int);
	struct gc_header *obj = malloc(total_size);
	if (obj == NULL)
		return NULL;

	obj->size = total_size;
	obj->ext_refs = 0;
	c->gc.all_mem += total_size;
	void *res = &obj->obj_start;
	rb_tree_insert(&c->gc.all_objects, res);
	return res;
}

struct chx_value *
cheax_alloc(CHEAX *c, size_t size)
{
	struct chx_value *res = cheax_simple_alloc(c, size);
	if (res != NULL)
		get_header(res)->is_var = false;
	return res;
}

struct variable *
cheax_alloc_var(CHEAX *c)
{
	struct variable *res = cheax_simple_alloc(c, sizeof(struct variable));
	if (res != NULL)
		get_header(res)->is_var = true;
	return res;
}

void
cheax_free(CHEAX *c, void *obj)
{
	struct gc_header *header = get_header(obj);
	rb_tree_remove(&c->gc.all_objects, obj);
	c->gc.all_mem -= header->size;
	free(header);
}

static void
to_gray(void *obj,
        struct rb_tree *white,
        struct rb_tree *gray)
{
	if (rb_tree_find(white, obj) != NULL) {
		rb_tree_remove(white, obj);
		rb_tree_insert(gray, obj);
	}
}

static void
mark(CHEAX *c,
     struct rb_tree *white,
     struct rb_tree *gray)
{
	if (gray->root == NULL)
		return;

	void *obj = gray->root->value;
	if (obj == NULL)
		return;

	/* done */
	rb_tree_remove(gray, obj);

	if (get_header(obj)->is_var) {
		struct variable *var = obj;
		if (var->below != NULL)
			to_gray(var->below, white, gray);

		if ((var->flags & CHEAX_SYNCED) == 0) {
			struct chx_value *val = var->value.norm;
			to_gray(val, white, gray);
		}

		/* if it has a name (which it probably does), it's
		 * probably from a chx_id, which we need to push to
		 * the gray heap too */

		char *str_bytes = (char *)var->name;
		if (str_bytes == NULL)
			return;

		void *id_bytes = str_bytes - sizeof(struct chx_id);
		if (rb_tree_find(white, id_bytes) == NULL)
			return; /* not relevant for us */

		struct chx_id *id = id_bytes;
		if (cheax_get_type(&id->base) != CHEAX_ID)
			return; /* id_bytes must be something else, then */

		to_gray(id, white, gray);

		return; /* rest of this function deals with chx_value */
	}

	struct chx_value *used = obj;

	struct chx_list *list;
	struct chx_func *func;
	struct chx_quote *quote;
	switch (cheax_resolve_type(c, cheax_get_type(used))) {
	case CHEAX_LIST:
		list = (struct chx_list *)used;
		to_gray(list->value, white, gray);
		to_gray(list->next, white, gray);
		break;

	case CHEAX_FUNC:
		func = (struct chx_func *)used;
		to_gray(func->args, white, gray);
		to_gray(func->body, white, gray);

		struct variable *var = func->locals_top;
		if (var != NULL)
			to_gray(var, white, gray);
		break;

	case CHEAX_QUOTE:
		quote = (struct chx_quote *)used;
		to_gray(quote->value, white, gray);
		break;
	}
}

static void
white_node_dealloc(struct rb_tree *white, struct rb_node *node)
{
	cheax_free(white->info, node->value);
	rb_node_dealloc(node);
}

void
cheax_gc(CHEAX *c)
{
	size_t mem = c->gc.all_mem;
	size_t prev = c->gc.prev_run;
	if (mem > prev && mem - prev >= GC_RUN_THRESHOLD)
		cheax_force_gc(c);
}

void
cheax_force_gc(CHEAX *c)
{
	struct rb_tree *white, *gray;
	white = rb_tree_create(obj_cmp);
	gray  = rb_tree_create(obj_cmp);

	struct rb_iter it;
	rb_iter_init(&it);
	for (void *obj = rb_iter_first(&it, &c->gc.all_objects);
	     obj != NULL;
	     obj = rb_iter_next(&it))
	{
		if (get_header(obj)->ext_refs > 0)
			rb_tree_insert(gray, obj);
		else
			rb_tree_insert(white, obj);
	}

	/* our root */
	if (c->locals_top != NULL)
		to_gray(c->locals_top, white, gray);

	/* mark */
	while (gray->root != NULL)
		mark(c, white, gray);

	rb_tree_dealloc(gray, rb_tree_node_dealloc_cb);

	/* sweep */
	white->info = c;
	rb_tree_dealloc(white, white_node_dealloc);

	c->gc.prev_run = c->gc.all_mem;
}
