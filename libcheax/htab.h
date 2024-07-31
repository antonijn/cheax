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

#ifndef HTAB_H
#define HTAB_H

#include <cheax.h>

/*
 * Add this to a struct to allow your data type to be used in used in
 * hash tables.
 *
 * Many functions in this API will only take or provide arguments of
 * type pointer to struct htab_entry, so you will need to find a way to
 * convert between lvalues of your data type and pointers to struct
 * htab_entry. The easiest way is probably to make struct htab_entry
 * the first field in your struct.
 */
struct htab_entry {
	struct htab_entry *next;
};

/*
 * Calculate hash for hash table entry. See also: cheax_good_hash_().
 */
typedef uint32_t (*htab_hash_func)(const struct htab_entry *item);

/*
 * Check whether two hash table entries are equal.
 */
typedef bool (*htab_eq_func)(const struct htab_entry *a, const struct htab_entry *b);

/*
 * Action function for hash table entries.
 */
typedef void (*htab_item_func)(struct htab_entry *item, void *data);

/*
 * Hash table structure. Do not access fields directly.
 */
struct htab {
	CHEAX *c;
	size_t size, cap;
	htab_hash_func hash;
	htab_eq_func eq;
	struct htab_entry **buckets;
	int cap_idx;
};

/*
 * Hash table look-up result.
 */
struct htab_search {
	struct htab_entry *item; /* Pointer to found item, or NULL if none found */
	struct htab_entry **pos; /* Insertion location, or NULL if there are no buckets */
	uint32_t hash;           /* Hash of key */
};

/*
 * Intialize hash table. This is very quick, since nothing is allocated
 * until the first entry is added to the table!
 */
void cheax_htab_init_(CHEAX *c, struct htab *htab, htab_hash_func hash, htab_eq_func eq);

/*
 * Clean up hash table, performing action `del' for each entry if `del'
 * is not NULL.
 */
void cheax_htab_cleanup_(struct htab *htab, htab_item_func del, void *data);

/*
 * Get hash table entry, or the location where a new one might be
 * inserted.
 */
struct htab_search cheax_htab_get_(struct htab *htab, const struct htab_entry *item);

/*
 * Set or add a hash table entry. Parameter `search' must be a value
 * given by cheax_htab_get_().
 */
void cheax_htab_set_(struct htab *htab, struct htab_search search, struct htab_entry *item);

/*
 * Remove a hash table entry. Parameter `search' must be a value given
 * by cheax_htab_get_().
 */
void cheax_htab_remove_(struct htab *htab, struct htab_search search);

/*
 * Perform an action for each entry in the hash table.
 */
void cheax_htab_foreach_(struct htab *htab, htab_item_func f, void *data);

/*
 * A reasonably good default hash function.
 */
uint32_t cheax_good_hash_(const void *p, size_t n);

#endif
