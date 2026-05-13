/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuOutputStream"

#include "config.h"

#include "fu-bytes.h"
#include "fu-output-stream.h"

/**
 * fu_output_stream_from_path:
 * @path: a filename
 * @error: (nullable): optional return location for an error
 *
 * Opens the file as an output stream.
 *
 * Returns: (transfer full): a #GOutputStream, or %NULL on error
 *
 * Since: 2.0.12
 **/
GOutputStream *
fu_output_stream_from_path(const gchar *path, GError **error)
{
	g_autoptr(GFile) file = NULL;
	g_autoptr(GFileOutputStream) stream = NULL;

	g_return_val_if_fail(path != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	file = g_file_new_for_path(path);
	stream = g_file_replace(file, NULL, FALSE, G_FILE_CREATE_NONE, NULL, error);
	if (stream == NULL) {
		fwupd_error_convert(error);
		return NULL;
	}
	return G_OUTPUT_STREAM(g_steal_pointer(&stream));
}

/**
 * fu_output_stream_write_bytes:
 * @stream: a #GOutputStream
 * @bytes: a #GBytes
 * @progress: (nullable): optional #FuProgress
 * @error: (nullable): optional return location for an error
 *
 * Write bytes into the stream, retrying as required. Will block during the operation.
 *
 * Returns: %TRUE for success
 *
 * Since: 2.0.12
 **/
gboolean
fu_output_stream_write_bytes(GOutputStream *stream,
			     GBytes *bytes,
			     FuProgress *progress,
			     GError **error)
{
	gsize bufsz;
	gsize total_written = 0;

	g_return_val_if_fail(G_IS_OUTPUT_STREAM(stream), FALSE);
	g_return_val_if_fail(bytes != NULL, FALSE);
	g_return_val_if_fail(progress == NULL || FU_IS_PROGRESS(progress), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	bufsz = g_bytes_get_size(bytes);
	do {
		gssize wrote;
		g_autoptr(GBytes) fw_data = NULL;

		fw_data = fu_bytes_new_offset(bytes, total_written, bufsz - total_written, error);
		if (fw_data == NULL)
			return FALSE;
		wrote = g_output_stream_write_bytes(stream, fw_data, NULL, error);
		if (wrote < 0) {
			fwupd_error_convert(error);
			return FALSE;
		}
		total_written += wrote;

		if (progress != NULL)
			fu_progress_set_percentage_full(progress, total_written, bufsz);
	} while (total_written < bufsz);
	if (total_written != bufsz) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "only wrote 0x%x of 0x%x",
			    (guint)total_written,
			    (guint)bufsz);
		return FALSE;
	}

	/* success */
	return TRUE;
}
