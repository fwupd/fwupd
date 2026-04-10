/*
 * Copyright 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
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

	/* get the size of all the chunks with overflow checking */
	for (guint i = 0; i < chunks->len; i++) {
		GBytes *chunk_tmp = g_ptr_array_index(chunks, i);
		gsize chunk_size = g_bytes_get_size(chunk_tmp);

		if (chunk_size > G_MAXSIZE - total_size) {
			g_critical("DFU chunk array total size overflow at chunk %u", i);
			return NULL;
		}
		total_size += chunk_size;
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

		/* check offset doesn't overflow */
		if (chunk_size > G_MAXUINT32 - offset) {
			g_critical("DFU chunk array offset overflow at chunk %u", i);
			g_free(buffer);
			return NULL;
		}

		memcpy(buffer + offset, chunk_data, chunk_size); /* nocheck:blocked */
		offset += chunk_size;
	}
	return g_bytes_new_take(buffer, total_size);
}
