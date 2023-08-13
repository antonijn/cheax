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

#include <stdint.h>
#include <string.h>

#include "attrib.h"
#include "core.h"
#include "htab.h"
#include "types.h"

static uint32_t
attrib_hash(const struct htab_entry *item)
{
	struct attrib *a = (struct attrib *)item;
	return cheax_good_hash_(&a->key, sizeof(void *));
}

static bool
attrib_eq(const struct htab_entry *ent_a, const struct htab_entry *ent_b)
{
	struct attrib *a = (struct attrib *)ent_a, *b = (struct attrib *)ent_b;
	return a->key == b->key;
}

void
cheax_attrib_init_(CHEAX *c)
{
	memset(c->attribs, 0, sizeof(c->attribs));

	for (int i = ATTRIB_FIRST; i <= ATTRIB_LAST; ++i) {
		cheax_htab_init_(c, &c->attribs[i].table, attrib_hash, attrib_eq);

		/* Assume payload is pointer-sized by default */
		c->attribs[i].size = offsetof(struct attrib, doc) + sizeof(void *);
	}

	c->attribs[ATTRIB_LOC].size = sizeof(struct attrib);
}

void
cheax_attrib_cleanup_(CHEAX *c)
{
	for (int i = ATTRIB_FIRST; i <= ATTRIB_LAST; ++i)
		cheax_htab_cleanup_(&c->attribs[i].table, NULL, NULL);
}

struct attrib *
cheax_attrib_add_(CHEAX *c, void *key, enum attrib_kind kind)
{
	ASSERT_NOT_NULL("attrib_add", key, NULL);

	unsigned rtflags = *(unsigned *)key;
	if (has_flag(rtflags, ATTRIB_BIT(kind))) {
		cheax_throwf(c, CHEAX_EAPI, "attrib_add(): attribute already present");
		return NULL;
	}

	struct attrib *entry = cheax_malloc(c, sizeof(struct attrib));
	cheax_ft(c, pad2);

	entry->key = key;

	struct htab_search search = cheax_htab_get_(&c->attribs[kind].table, &entry->entry);
	cheax_htab_set_(&c->attribs[kind].table, search, &entry->entry);
	cheax_ft(c, pad);

	*(unsigned *)key = rtflags | ATTRIB_BIT(kind);
	return entry;
pad:
	cheax_free(c, entry);
pad2:
	return NULL;
}

void
cheax_attrib_remove_(CHEAX *c, void *key, enum attrib_kind kind)
{
	unsigned rtflags = *(unsigned *)key;
	if (key == NULL || !has_flag(rtflags, ATTRIB_BIT(kind)))
		return;

	struct attrib dummy = { .key = key };
	struct htab_search search = cheax_htab_get_(&c->attribs[kind].table, &dummy.entry);
	cheax_htab_remove_(&c->attribs[kind].table, search);

	*(unsigned *)key = rtflags & ~ATTRIB_BIT(kind);
}

void
cheax_attrib_remove_all_(CHEAX *c, void *key)
{
	for (int i = ATTRIB_FIRST; i <= ATTRIB_LAST; ++i)
		cheax_attrib_remove_(c, key, i);
}

struct attrib *
cheax_attrib_get_(CHEAX *c, void *key, enum attrib_kind kind)
{
	if (key == NULL || !has_flag(*(unsigned *)key, ATTRIB_BIT(kind)))
		return NULL;

	struct attrib dummy = { .key = key };
	struct htab_search search = cheax_htab_get_(&c->attribs[kind].table, &dummy.entry);
	return (search.item == NULL)
	     ? NULL
	     : container_of(search.item, struct attrib, entry);
}

struct attrib *
cheax_attrib_copy_(CHEAX *c, void *dst_key, void *src_key, enum attrib_kind kind)
{
	cheax_attrib_remove_(c, dst_key, kind);
	struct attrib *src_attr = cheax_attrib_get_(c, src_key, kind);
	struct attrib *dst_attr = cheax_attrib_add_(c, dst_key, kind);
	size_t content_size = c->attribs[kind].size - offsetof(struct attrib, doc);
	memcpy(&dst_attr->doc, &src_attr->doc, content_size);
	return dst_attr;
}
