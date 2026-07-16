/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuMemoryInputStream"

#include "config.h"

#include "fu-memory-input-stream.h"

/**
 * fu_memory_input_stream_new:
 *
 * Creates a new empty memory-backed input stream.
 *
 * Returns: (transfer full): a #FuInputStream
 *
 * Since: 2.1.7
 **/
FuInputStream *
fu_memory_input_stream_new(void)
{
	return g_memory_input_stream_new(); /* nocheck:blocked */
}

/**
 * fu_memory_input_stream_new_from_bytes:
 * @bytes: a #GBytes
 *
 * Creates a new memory-backed input stream from @bytes.
 *
 * Returns: (transfer full): a #FuInputStream
 *
 * Since: 2.1.7
 **/
FuInputStream *
fu_memory_input_stream_new_from_bytes(GBytes *bytes)
{
	g_return_val_if_fail(bytes != NULL, NULL);
	return g_memory_input_stream_new_from_bytes(bytes); /* nocheck:blocked */
}

/**
 * fu_memory_input_stream_new_from_data:
 * @data: (array length=len) (element-type guint8): input data
 * @len: length of the data, or -1 if @data is a nul-terminated string
 * @destroy: (nullable): function that is called to free @data, or %NULL
 *
 * Creates a new memory-backed input stream from @data.
 *
 * Returns: (transfer full): a #FuInputStream
 *
 * Since: 2.1.7
 **/
FuInputStream *
fu_memory_input_stream_new_from_data(const void *data, gssize len, GDestroyNotify destroy)
{
	g_return_val_if_fail(data != NULL, NULL);
	return g_memory_input_stream_new_from_data(data, len, destroy); /* nocheck:blocked */
}
