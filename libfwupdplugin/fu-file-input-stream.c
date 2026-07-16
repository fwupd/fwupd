/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuFileInputStream"

#include "config.h"

#include "fu-file-input-stream.h"

/**
 * fu_file_input_stream_from_file:
 * @file: a #GFile
 * @cancellable: (nullable): optional #GCancellable
 * @error: (nullable): optional return location for an error
 *
 * Opens a #GFile for reading and returns a #FuFileInputStream. Use
 * this instead of g_file_read().
 *
 * Returns: (transfer full): a #FuFileInputStream, or %NULL on error
 *
 * Since: 2.1.7
 **/
FuFileInputStream *
fu_file_input_stream_from_file(GFile *file, GCancellable *cancellable, GError **error)
{
	g_return_val_if_fail(G_IS_FILE(file), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);
	return g_file_read(file, cancellable, error); /* nocheck:blocked */
}

/**
 * fu_file_input_stream_query_info:
 * @stream: a #FuFileInputStream
 * @attributes: a file attribute query string
 * @cancellable: (nullable): optional #GCancellable
 * @error: (nullable): optional return location for an error
 *
 * Queries a file input stream for the given @attributes.
 *
 * Returns: (transfer full): a #GFileInfo, or %NULL on error
 *
 * Since: 2.1.7
 **/
GFileInfo *
fu_file_input_stream_query_info(FuFileInputStream *stream,
				const gchar *attributes,
				GCancellable *cancellable,
				GError **error)
{
	g_return_val_if_fail(FU_IS_FILE_INPUT_STREAM(stream), NULL);
	g_return_val_if_fail(attributes != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);
	return g_file_input_stream_query_info(G_FILE_INPUT_STREAM(stream), /* nocheck:blocked */
					      attributes,
					      cancellable,
					      error);
}
