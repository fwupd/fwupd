/*
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fu-dfu-common.h"

/**
 * fu_dfu_utils_bytes_join_array:
 * @chunks: (element-type GBytes): bytes
 *
 * Creates a monolithic block of memory from an array of #GBytes.
 *
 * Returns: (transfer full): a new GBytes
 **/
GBytes *
fu_dfu_utils_bytes_join_array(GPtrArray *chunks)
{
	gsize total_size = 0;
	guint32 offset = 0;
	guint8 *buffer;

	/* get the size of all the chunks */
	for (guint i = 0; i < chunks->len; i++) {
		GBytes *chunk_tmp = g_ptr_array_index(chunks, i);
		total_size += g_bytes_get_size(chunk_tmp);
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
		memcpy(buffer + offset, chunk_data, chunk_size);
		offset += chunk_size;
	}
	return g_bytes_new_take(buffer, total_size);
}
