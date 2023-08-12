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

#include "htab.h"

/*
 * This is taken from https://planetmath.org/goodhashtableprimes, with
 * the values up to 29 added by yours truly.
 */
static const size_t capacities[] = {
	0, 5, 11, 17, 29, 53, 97, 193, 389, 769, 1543, 3079, 6151, 12289,
	24593, 49157, 98317, 196613, 393241, 786433, 1572869, 3145739,
	6291469, 12582917, 50331653, 100663319, 201326611, 402653189,
	805306457, 1610612741,
};

void
htab_init(CHEAX *c, struct htab *htab, htab_hash_func hash, htab_eq_func eq)
{
	htab->c = c;
	htab->hash = hash;
	htab->eq = eq;
	htab->size = 0;
	htab->buckets = NULL;
	htab->cap = 0;
	htab->cap_idx = 0;
}

void
htab_cleanup(struct htab *htab, htab_item_func del, void *data)
{
	for (size_t i = 0; i < htab->cap; ++i) {
		struct htab_entry *bucket, *next;
		for (bucket = htab->buckets[i]; bucket != NULL; bucket = next) {
			next = bucket->next;
			if (del != NULL)
				del(bucket, data);
		}
	}

	cheax_free(htab->c, htab->buckets);
	memset(htab, 0, sizeof(*htab));
}

uint32_t
htab_get(struct htab *htab, const struct htab_entry *item, struct htab_entry ***ep)
{
	static struct htab_entry *const sentinel = NULL;
	size_t h = htab->hash(item);

	if (htab->cap == 0) {
		*ep = (struct htab_entry **)&sentinel;
		return h;
	}

	struct htab_entry **b, *e;
	for (b = &htab->buckets[(size_t)h % htab->cap]; (e = *b) != NULL; b = &e->next) {
		if (htab->hash(e) == h && htab->eq(item, e))
			break;
	}

	*ep = b;
	return h;
}

static void
resize(struct htab *htab, int new_cap_idx)
{
	size_t new_cap = capacities[new_cap_idx];
	struct htab_entry **new_buckets = cheax_calloc(htab->c,
	                                               new_cap,
	                                               sizeof(struct htab_entry *));
	if (new_buckets == NULL) {
		if (new_cap < htab->cap)
			cheax_clear_errno(htab->c);
		return;
	}

	for (size_t i = 0; i < htab->cap; ++i) {
		struct htab_entry *b, *next;
		for (b = htab->buckets[i]; b != NULL; b = next) {
			size_t new_idx = htab->hash(b) % new_cap;
			next = b->next;

			b->next = new_buckets[new_idx];
			new_buckets[new_idx] = b;
		}
	}

	cheax_free(htab->c, htab->buckets);
	htab->buckets = new_buckets;
	htab->cap = new_cap;
	htab->cap_idx = new_cap_idx;
}

/*
 * Returns whether resizing was necessary.
 */
static bool
resize_up(struct htab *htab)
{
	if (htab->size >= htab->cap - htab->cap / 4) {
		if (htab->cap_idx + 1 < (int)(sizeof(capacities) / sizeof(capacities[0])))
			resize(htab, htab->cap_idx + 1);
		else
			cheax_throwf(htab->c, CHEAX_ENOMEM, "hash table too big");
		return true;
	}
	return false;
}

static void
resize_down(struct htab *htab)
{
	if (htab->size < (htab->cap - htab->cap / 4) / 4 && htab->cap_idx > 1)
		resize(htab, htab->cap_idx - 1);
}

void
htab_set(struct htab *htab, struct htab_entry **entry, struct htab_entry *item, uint32_t hash)
{
	if (*entry == NULL) {
		++htab->size;
		if (resize_up(htab)) {
			if (cheax_errno(htab->c) != 0)
				return;
			entry = &htab->buckets[(size_t)hash % htab->cap];
		}
	}

	item->next = *entry;
	*entry = item;
}

void
htab_remove(struct htab *htab, struct htab_entry **entry)
{
	struct htab_entry *e = *entry;
	if (e != NULL) {
		*entry = e->next;
		e->next = NULL;
		resize_down(htab);
	}
}

void
htab_foreach(struct htab *htab, htab_item_func f, void *data)
{
	for (size_t i = 0; i < htab->cap; ++i) {
		for (struct htab_entry *e = htab->buckets[i]; e != NULL; e = e->next)
			f(e, data);
	}
}

uint32_t
good_hash(const void *p, size_t n)
{
	/* Daniel J. Bernstein hash (djb2) */
	uint32_t hash = 5381U;
	const unsigned char *cp = p;

	for (size_t i = 0; i < n; ++i)
		hash = hash * 33U + cp[i];

	return hash;
}
