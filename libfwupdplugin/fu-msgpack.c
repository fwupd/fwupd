/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuMsgpack"

#include "config.h"

#include "fu-msgpack-item-private.h"
#include "fu-msgpack.h"

/**
 * fu_msgpack_new:
 * @buf: data blob
 * @error: (nullable): optional return location for an error
 *
 * Parses a buffer into messagepack items.
 *
 * Returns: (transfer container) (element-type FuMsgpackItem): items, or %NULL on error
 *
 * Since: 2.0.0
 **/
GPtrArray *
fu_msgpack_parse(GByteArray *buf, GError **error)
{
	gsize offset = 0;
	g_autoptr(GPtrArray) items = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);

	g_return_val_if_fail(buf != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	while (offset < buf->len) {
		g_autoptr(FuMsgpackItem) item = NULL;
		item = fu_msgpack_item_parse(buf, &offset, error);
		if (item == NULL) {
			g_prefix_error(error, "offset 0x%x: ", (guint)offset);
			return NULL;
		}
		g_ptr_array_add(items, g_steal_pointer(&item));
	}

	return g_steal_pointer(&items);
}

/**
 * fu_msgpack_write:
 * @items: (element-type FuMsgpackItem): items
 * @error: (nullable): optional return location for an error
 *
 * Writes messagepack items into a buffer.
 *
 * Returns: (transfer container): buffer, or %NULL on error
 *
 * Since: 2.0.0
 **/
GByteArray *
fu_msgpack_write(GPtrArray *items, GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();

	g_return_val_if_fail(items != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	for (guint i = 0; i < items->len; i++) {
		FuMsgpackItem *item = g_ptr_array_index(items, i);
		if (!fu_msgpack_item_append(item, buf, error))
			return NULL;
	}

	/* success */
	return g_steal_pointer(&buf);
}

/**
 * fu_msgpack_map_lookup:
 * @items: (element-type FuMsgpackItem): items
 * @idx: index into the items, usually 0
 * @key: (not nullable): key to find
 * @error: (nullable): optional return location for an error
 *
 * Looks up an item from a map. This is similar in action to looking up an `a{sv}` dictionary
 * with g_variant_lookup().
 *
 * Returns: (transfer full): a #FuMsgpackItem, or %NULL on error
 *
 * Since: 2.0.0
 **/
FuMsgpackItem *
fu_msgpack_map_lookup(GPtrArray *items, guint idx, const gchar *key, GError **error)
{
	guint64 map_size = 0;
	FuMsgpackItem *item_map;

	g_return_val_if_fail(items != NULL, NULL);
	g_return_val_if_fail(key != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* sanity check */
	if (idx >= items->len) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "index %u of %u would be invalid",
			    idx,
			    items->len);
		return NULL;
	}

	/* verify is a map */
	item_map = g_ptr_array_index(items, idx);
	if (fu_msgpack_item_get_kind(item_map) != FU_MSGPACK_ITEM_KIND_MAP) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "is not a map");
		return NULL;
	}

	/* read each {sv} */
	map_size = fu_msgpack_item_get_map(item_map);
	if (idx + (map_size * 2) >= items->len) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "map %u with index %u of %u would be invalid",
			    (guint)map_size,
			    idx,
			    items->len);
		return NULL;
	}
	for (guint i = idx + 1; i < idx + (map_size * 2); i += 2) {
		FuMsgpackItem *item_key = g_ptr_array_index(items, i);
		FuMsgpackItem *item_value = g_ptr_array_index(items, i + 1);
		FuMsgpackItemKind kind_key = fu_msgpack_item_get_kind(item_key);

		if (kind_key != FU_MSGPACK_ITEM_KIND_STRING) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "at index %u, key is not a string, got %s",
				    i,
				    fu_msgpack_item_kind_to_string(kind_key));
			return NULL;
		}
		if (g_strcmp0(fu_msgpack_item_get_string(item_key)->str, key) == 0)
			return g_object_ref(item_value);
	}
	g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, "no key %s in map", key);
	return NULL;
}
