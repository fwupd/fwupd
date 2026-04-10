/*
 * Copyright 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-dfu-common.h"

/**
 * fu_dfu_utils_bytes_join_array:
 * @chunks: (element-type GBytes): bytes
 * @error: (nullable): optional return location for an error
 *
 * Creates a monolithic block of memory from an array of #GBytes.
 *
 * Returns: (transfer full): a new GBytes, or %NULL on error
 **/
GBytes *
fu_dfu_utils_bytes_join_array(GPtrArray *chunks, GError **error)
{
	gsize total_size = 0;
	gsize offset = 0;
	g_autofree guint8 *buffer = NULL;

	/* get the size of all the chunks */
	for (guint i = 0; i < chunks->len; i++) {
		GBytes *chunk_tmp = g_ptr_array_index(chunks, i);
		gsize chunk_size = g_bytes_get_size(chunk_tmp);
		if (chunk_size > G_MAXSIZE - total_size) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "buffer too large");
			return NULL;
		}
		if (!fu_size_checked_inc(&total_size, chunk_size, error))
			return NULL;
	}

	/* copy them into a buffer */
	buffer = g_malloc0(total_size);
	for (guint i = 0; i < chunks->len; i++) {
		const guint8 *chunk_data;
		gsize chunk_size = 0;
		GBytes *chunk_tmp = g_ptr_array_index(chunks, i);
		chunk_data = g_bytes_get_data(chunk_tmp, &chunk_size);
		if (chunk_size == 0)
			continue;
		if (!fu_memcpy_safe(buffer,
				    total_size,
				    offset,
				    chunk_data,
				    chunk_size,
				    0x0,
				    chunk_size,
				    error))
			return NULL;
		if (!fu_size_checked_inc(&offset, chunk_size, error))
			return NULL;
	}
	return g_bytes_new_take(g_steal_pointer(&buffer), total_size);
}
