/*
 * Based on Julienne Walker's <http://eternallyconfuzzled.com/> rb_tree
 * implementation.
 *
 * Modified by Mirek Rusin <http://github.com/mirek/rb_tree>.
 * Style modifications by Antonie Blom.
 *
 * This is free and unencumbered software released into the public domain.
 *
 * Anyone is free to copy, modify, publish, use, compile, sell, or
 * distribute this software, either in source code form or as a compiled
 * binary, for any purpose, commercial or non-commercial, and by any
 * means.
 *
 * In jurisdictions that recognize copyright laws, the author or authors
 * of this software dedicate any and all copyright interest in the
 * software to the public domain. We make this dedication for the benefit
 * of the public at large and to the detriment of our heirs and
 * successors. We intend this dedication to be an overt act of
 * relinquishment in perpetuity of all present and future rights to this
 * software under copyright law.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * For more information, please refer to <http://unlicense.org/>
 */

#include "rbtree.h"

/* rb_node */

struct rb_node *
rb_node_alloc()
{
	return malloc(sizeof(struct rb_node));
}

struct rb_node *
rb_node_init(struct rb_node *self, void *value)
{
	if (self != NULL) {
		self->red = 1;
		self->link[0] = self->link[1] = NULL;
		self->value = value;
	}
	return self;
}

struct rb_node *
rb_node_create(void *value)
{
	return rb_node_init(rb_node_alloc(), value);
}

void
rb_node_dealloc(struct rb_node *self)
{
	free(self);
}

static bool
rb_node_is_red(const struct rb_node *self)
{
	return (self != NULL) ? self->red : false;
}

static struct rb_node *
rb_node_rotate(struct rb_node *self, int dir)
{
	if (self != NULL) {
		struct rb_node *result = NULL;
		result = self->link[!dir];
		self->link[!dir] = result->link[dir];
		result->link[dir] = self;
		self->red = 1;
		result->red = 0;
		return result;
	}

	return NULL;
}

static struct rb_node *
rb_node_rotate2(struct rb_node *self, int dir)
{
	if (self != NULL) {
		struct rb_node *result = NULL;
		self->link[!dir] = rb_node_rotate(self->link[!dir], !dir);
		result = rb_node_rotate(self, dir);
		return result;
	}

	return NULL;
}

/* rb_tree - default callbacks */

int
rb_tree_node_cmp_ptr_cb(struct rb_tree *self, struct rb_node *a, struct rb_node *b)
{
	return (a->value > b->value) - (a->value < b->value);
}

void
rb_tree_node_dealloc_cb(struct rb_tree *self, struct rb_node *node)
{
	if (self != NULL && node != NULL)
		rb_node_dealloc(node);
}

/* rb_tree */

struct rb_tree *
rb_tree_alloc()
{
	return malloc(sizeof(struct rb_tree));
}

struct rb_tree *
rb_tree_init(struct rb_tree *self, rb_tree_node_cmp_f node_cmp_cb)
{
	if (self != NULL) {
		self->root = NULL;
		self->size = 0;
		self->cmp = node_cmp_cb ? node_cmp_cb : rb_tree_node_cmp_ptr_cb;
	}

	return self;
}

struct rb_tree *
rb_tree_create(rb_tree_node_cmp_f node_cb)
{
	return rb_tree_init(rb_tree_alloc(), node_cb);
}

void
rb_tree_cleanup(struct rb_tree *self, rb_tree_node_f node_cb)
{
	if (self == NULL || node_cb == NULL)
		return;

	struct rb_node *save = NULL;

	/* Rotate away the left links so that
	 * we can treat this like the destruction
	 * of a linked list */
	for (struct rb_node *node = self->root; node != NULL; node = save) {
		if (node->link[0] != NULL) {
			/* Rotate away the left link and check again */
			save = node->link[0];
			node->link[0] = save->link[1];
			save->link[1] = node;
		} else {
			/* No left links, just kill the node and move on */
			save = node->link[1];
			node_cb(self, node);
			node = NULL;
		}
	}
}

void
rb_tree_dealloc(struct rb_tree *self, rb_tree_node_f node_cb)
{
	rb_tree_cleanup(self, node_cb);
	free(self);
}


int
rb_tree_test(struct rb_tree *self, struct rb_node *root)
{
	if (root == NULL)
		return 1;

	struct rb_node *ln = root->link[0];
	struct rb_node *rn = root->link[1];

	/* Consecutive red links */
	if (rb_node_is_red(root)) {
		if (rb_node_is_red(ln) || rb_node_is_red(rn)) {
			fprintf(stderr, "Red violation");
			return 0;
		}
	}

	int lh = rb_tree_test(self, ln);
	int rh = rb_tree_test(self, rn);

	/* Invalid binary search tree */
	if ((ln != NULL && self->cmp(self, ln, root) >= 0)
	 || (rn != NULL && self->cmp(self, rn, root) <= 0))
	{
		fprintf(stderr, "Binary tree violation");
		return 0;
	}

	/* Black height mismatch */
	if (lh > 0 && rh > 0 && lh != rh) {
		fprintf(stderr, "Black violation");
		return 0;
	}

	/* Only count black links */
	if (lh > 0 && rh > 0)
		return rb_node_is_red(root) ? lh : lh + 1;

	return 0;
}

void *
rb_tree_find(struct rb_tree *self, void *value)
{
	if (self == NULL)
		return NULL;

	struct rb_node node = { .value = value };
	struct rb_node *it = self->root;
	while (it != NULL) {
		int cmp = self->cmp(self, it, &node);
		if (cmp == 0)
			return it->value;

		/* If the tree supports duplicates, they should be
		 * chained to the right subtree for this to work */
		it = it->link[cmp < 0];
	}

	return NULL;
}

/* Creates (malloc'ates) */
void
rb_tree_insert(struct rb_tree *self, void *value)
{
	rb_tree_insert_node(self, rb_node_create(value));
}

void
rb_tree_insert_node(struct rb_tree *self, struct rb_node *node)
{
	if (self == NULL || node == NULL)
		return;

	if (self->root == NULL) {
		self->root = node;
		goto done;
	}

	struct rb_node head = { 0 };
	int dir = 0, last = 0;

	/* Set up our helpers */
	struct rb_node *t = &head;
	struct rb_node *g = NULL, *p = NULL;
	struct rb_node *q = t->link[1] = self->root;

	/* Search down the tree for a place to insert */
	for (;;) {
		if (q == NULL) {

			/* Insert node at the first null link. */
			p->link[dir] = q = node;
		} else if (rb_node_is_red(q->link[0]) && rb_node_is_red(q->link[1])) {

			/* Simple red violation: color flip */
			q->red = true;
			q->link[0]->red = false;
			q->link[1]->red = false;
		}

		if (rb_node_is_red(q) && rb_node_is_red(p)) {

			/* Hard red violation: rotations necessary */
			int dir2 = t->link[1] == g;
			if (q == p->link[last])
				t->link[dir2] = rb_node_rotate(g, !last);
			else
				t->link[dir2] = rb_node_rotate2(g, !last);
		}

		/* Stop working if we inserted a node. This
		 * check also disallows duplicates in the tree */
		if (self->cmp(self, q, node) == 0)
			break;

		last = dir;
		dir = self->cmp(self, q, node) < 0;

		/* Move the helpers down */
		if (g != NULL)
			t = g;

		g = p, p = q;
		q = q->link[dir];
	}

	/* Update the root (it may be different) */
	self->root = head.link[1];
done:
	/* Make the root black for simplified logic */
	self->root->red = false;
	++self->size;
}

/* Returns 1 if the value was removed, 0 otherwise. Optional node callback
 * can be provided to dealloc node and/or user data. Use rb_tree_node_dealloc
 * default callback to deallocate node created by rb_tree_insert(...). */
bool
rb_tree_remove_with_cb(struct rb_tree *self, void *value, rb_tree_node_f node_cb)
{
	if (self->root == NULL)
		return false;

	struct rb_node head = { 0 }; /* False tree root */
	struct rb_node node = { .value = value }; /* Value wrapper node */
	int dir = 1;

	/* Set up our helpers */
	struct rb_node *f = NULL; /* Found item */
	struct rb_node *q = &head;
	struct rb_node *g = NULL, *p = NULL;
	q->link[1] = self->root;

	/* Search and push a red node down
	 * to fix red violations as we go */
	while (q->link[dir] != NULL) {
		int last = dir;

		/* Move the helpers down */
		g = p, p = q;
		q = q->link[dir];
		dir = self->cmp(self, q, &node) < 0;

		/* Save the node with matching value and keep
		 * going; we'll do removal tasks at the end */
		if (self->cmp(self, q, &node) == 0)
			f = q;

		/* Push the red node down with rotations and color flips */
		if (rb_node_is_red(q) || rb_node_is_red(q->link[dir]))
			continue;

		struct rb_node *s = p->link[!last];

		if (rb_node_is_red(q->link[!dir])) {
			p = p->link[last] = rb_node_rotate(q, dir);
			continue;
		}

		if (s == NULL)
			continue;

		if (!rb_node_is_red(s->link[!last]) && !rb_node_is_red(s->link[last])) {

			/* Color flip */
			p->red = 0;
			s->red = 1;
			q->red = 1;
			continue;
		}

		int dir2 = g->link[1] == p;

		if (rb_node_is_red(s->link[last]))
			g->link[dir2] = rb_node_rotate2(p, last);
		else if (rb_node_is_red(s->link[!last]))
			g->link[dir2] = rb_node_rotate(p, last);

		/* Ensure correct coloring */
		q->red = g->link[dir2]->red = 1;
		g->link[dir2]->link[0]->red = 0;
		g->link[dir2]->link[1]->red = 0;
	}

	/* Replace and remove the saved node */
	if (f != NULL) {
		void *tmp = f->value;
		f->value = q->value;
		q->value = tmp;

		p->link[p->link[1] == q] = q->link[q->link[0] == NULL];

		if (node_cb != NULL)
			node_cb(self, q);

		q = NULL;
	}

	/* Update the root (it may be different) */
	self->root = head.link[1];

	/* Make the root black for simplified logic */
	if (self->root != NULL)
		self->root->red = false;

	--self->size;
	return true;
}

bool
rb_tree_remove(struct rb_tree *self, void *value)
{
	if (self == NULL)
		return false;

	return rb_tree_remove_with_cb(self, value, rb_tree_node_dealloc_cb);
}

size_t
rb_tree_size(struct rb_tree *self)
{
	if (self == NULL)
		return 0;

	return self->size;
}

/* rb_iter */

struct rb_iter *
rb_iter_alloc()
{
	return malloc(sizeof(struct rb_iter));
}

struct rb_iter *
rb_iter_init(struct rb_iter *self)
{
	if (self != NULL) {
		self->tree = NULL;
		self->node = NULL;
		self->top = 0;
	}
	return self;
}

struct rb_iter *
rb_iter_create()
{
	return rb_iter_init(rb_iter_alloc());
}

void
rb_iter_dealloc(struct rb_iter *self)
{
	free(self);
}

/* Internal function, init traversal object, dir determines whether
 * to begin traversal at the smallest or largest valued node. */
static void *
rb_iter_start(struct rb_iter *self, struct rb_tree *tree, int dir)
{
	if (self == NULL)
		return NULL;

	self->tree = tree;
	self->node = tree->root;
	self->top = 0;

	/* Save the path for later selfersal */
	if (self->node != NULL) {
		while (self->node->link[dir] != NULL) {
			self->path[self->top++] = self->node;
			self->node = self->node->link[dir];
		}
	}

	return (self->node == NULL) ? NULL : self->node->value;
}

/* Traverse a red black tree in the user-specified direction (0 asc, 1 desc) */
static void *
rb_iter_move(struct rb_iter *self, int dir)
{
	if (self->node->link[dir] != NULL) {

		/* Continue down this branch */
		self->path[self->top++] = self->node;
		self->node = self->node->link[dir];
		while (self->node->link[!dir] != NULL) {
			self->path[self->top++] = self->node;
			self->node = self->node->link[!dir];
		}

		return (self->node == NULL) ? NULL : self->node->value;
	}

	/* Move to the next branch */
	struct rb_node *last = NULL;
	do {
		if (self->top == 0) {
			self->node = NULL;
			break;
		}
		last = self->node;
		self->node = self->path[--self->top];
	} while (last == self->node->link[dir]);

	return (self->node == NULL) ? NULL : self->node->value;
}

void *
rb_iter_first(struct rb_iter *self, struct rb_tree *tree)
{
	return rb_iter_start(self, tree, 0);
}

void *
rb_iter_last(struct rb_iter *self, struct rb_tree *tree)
{
	return rb_iter_start(self, tree, 1);
}

void *
rb_iter_next(struct rb_iter *self)
{
	return rb_iter_move(self, 1);
}

void *
rb_iter_prev(struct rb_iter *self)
{
	return rb_iter_move(self, 0);
}
