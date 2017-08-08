/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <config.h>

#include <gio/gunixinputstream.h>
#include <archive_entry.h>
#include <archive.h>

#include "fwupd-error.h"

#include "fu-common.h"

/**
 * fu_common_set_contents_bytes:
 * @filename: A filename
 * @bytes: The data to write
 * @error: A #GError, or %NULL
 *
 * Writes a blob of data to a filename, creating the parent directories as
 * required.
 *
 * Returns: %TRUE for success
 **/
gboolean
fu_common_set_contents_bytes (const gchar *filename, GBytes *bytes, GError **error)
{
	const gchar *data;
	gsize size;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GFile) file_parent = NULL;

	file = g_file_new_for_path (filename);
	file_parent = g_file_get_parent (file);
	if (!g_file_query_exists (file_parent, NULL)) {
		if (!g_file_make_directory_with_parents (file_parent, NULL, error))
			return FALSE;
	}
	data = g_bytes_get_data (bytes, &size);
	return g_file_set_contents (filename, data, size, error);
}

/**
 * fu_common_get_contents_bytes:
 * @filename: A filename
 * @error: A #GError, or %NULL
 *
 * Reads a blob of data from a file.
 *
 * Returns: a #GBytes, or %NULL for failure
 **/
GBytes *
fu_common_get_contents_bytes (const gchar *filename, GError **error)
{
	gchar *data = NULL;
	gsize len = 0;
	if (!g_file_get_contents (filename, &data, &len, error))
		return NULL;
	return g_bytes_new_take (data, len);
}

/**
 * fu_common_get_contents_fd:
 * @fd: A file descriptor
 * @count: The maximum number of bytes to read
 * @error: A #GError, or %NULL
 *
 * Reads a blob from a specific file descriptor.
 *
 * Note: this will close the fd when done
 *
 * Returns: (transfer container): a #GBytes, or %NULL
 **/
GBytes *
fu_common_get_contents_fd (gint fd, gsize count, GError **error)
{
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GInputStream) stream = NULL;

	g_return_val_if_fail (fd > 0, NULL);
	g_return_val_if_fail (count > 0, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* read the entire fd to a data blob */
	stream = g_unix_input_stream_new (fd, TRUE);
	blob = g_input_stream_read_bytes (stream, count, NULL, &error_local);
	if (blob == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     error_local->message);
		return NULL;
	}
	return g_steal_pointer (&blob);
}

static gboolean
fu_common_extract_archive_entry (struct archive_entry *entry, const gchar *dir)
{
	const gchar *tmp;
	g_autofree gchar *buf = NULL;

	/* no output file */
	if (archive_entry_pathname (entry) == NULL)
		return FALSE;

	/* update output path */
	tmp = archive_entry_pathname (entry);
	buf = g_build_filename (dir, tmp, NULL);
	archive_entry_update_pathname_utf8 (entry, buf);
	return TRUE;
}

/**
 * fu_common_extract_archive:
 * @blob: a #GBytes archive as a blob
 * @directory: a directory name to extract to
 * @error: A #GError, or %NULL
 *
 * Extracts an achive to a directory.
 *
 * Returns: %TRUE for success
 **/
gboolean
fu_common_extract_archive (GBytes *blob, const gchar *dir, GError **error)
{
	gboolean ret = TRUE;
	int r;
	struct archive *arch = NULL;
	struct archive_entry *entry;

	/* decompress anything matching either glob */
	arch = archive_read_new ();
	archive_read_support_format_all (arch);
	archive_read_support_filter_all (arch);
	r = archive_read_open_memory (arch,
				      (void *) g_bytes_get_data (blob, NULL),
				      (size_t) g_bytes_get_size (blob));
	if (r != 0) {
		ret = FALSE;
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "Cannot open: %s",
			     archive_error_string (arch));
		goto out;
	}
	for (;;) {
		gboolean valid;
		g_autofree gchar *path = NULL;
		r = archive_read_next_header (arch, &entry);
		if (r == ARCHIVE_EOF)
			break;
		if (r != ARCHIVE_OK) {
			ret = FALSE;
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Cannot read header: %s",
				     archive_error_string (arch));
			goto out;
		}

		/* only extract if valid */
		valid = fu_common_extract_archive_entry (entry, dir);
		if (!valid)
			continue;
		r = archive_read_extract (arch, entry, 0);
		if (r != ARCHIVE_OK) {
			ret = FALSE;
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Cannot extract: %s",
				     archive_error_string (arch));
			goto out;
		}
	}
out:
	if (arch != NULL) {
		archive_read_close (arch);
		archive_read_free (arch);
	}
	return ret;
}
