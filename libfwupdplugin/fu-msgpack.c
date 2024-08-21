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
