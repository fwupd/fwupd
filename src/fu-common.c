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
#include <glib/gstdio.h>

#include <archive_entry.h>
#include <archive.h>
#include <errno.h>

#include "fwupd-error.h"

#include "fu-common.h"

/**
 * SECTION:fu-common
 * @short_description: common functionality for plugins to use
 *
 * Helper functions that can be used by the daemon and plugins.
 *
 * See also: #FuPlugin
 */

/**
 * fu_common_rmtree:
 * @directory: a directory name
 * @error: A #GError or %NULL
 *
 * Recursively removes a directory.
 *
 * Returns: %TRUE for success, %FALSE otherwise
 **/
gboolean
fu_common_rmtree (const gchar *directory, GError **error)
{
	const gchar *filename;
	g_autoptr(GDir) dir = NULL;

	/* try to open */
	g_debug ("removing %s", directory);
	dir = g_dir_open (directory, 0, error);
	if (dir == NULL)
		return FALSE;

	/* find each */
	while ((filename = g_dir_read_name (dir))) {
		g_autofree gchar *src = NULL;
		src = g_build_filename (directory, filename, NULL);
		if (g_file_test (src, G_FILE_TEST_IS_DIR)) {
			if (!fu_common_rmtree (src, error))
				return FALSE;
		} else {
			if (g_unlink (src) != 0) {
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INTERNAL,
					     "Failed to delete: %s", src);
				return FALSE;
			}
		}
	}
	if (g_remove (directory) != 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "Failed to delete: %s", directory);
		return FALSE;
	}
	return TRUE;
}

/**
 * fu_common_mkdir_parent:
 * @filename: A full pathname
 * @error: A #GError, or %NULL
 *
 * Creates any required directories, including any parent directories.
 *
 * Returns: %TRUE for success
 **/
gboolean
fu_common_mkdir_parent (const gchar *filename, GError **error)
{
	g_autofree gchar *parent = NULL;

	parent = g_path_get_dirname (filename);
	g_debug ("creating path %s", parent);
	if (g_mkdir_with_parents (parent, 0755) == -1) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "Failed to create '%s': %s",
			     parent, g_strerror (errno));
		return FALSE;
	}
	return TRUE;
}

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
	g_debug ("writing %s with %" G_GSIZE_FORMAT " bytes", filename, size);
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
	g_debug ("reading %s with %" G_GSIZE_FORMAT " bytes", filename, len);
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
 * Returns: (transfer full): a #GBytes, or %NULL
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
 * @dir: a directory name to extract to
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
	g_debug ("decompressing into %s", dir);
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

static void
fu_common_add_argv (GPtrArray *argv, const gchar *fmt, ...) G_GNUC_PRINTF (2, 3);

static void
fu_common_add_argv (GPtrArray *argv, const gchar *fmt, ...)
{
	va_list args;
	g_autofree gchar *tmp = NULL;
	g_auto(GStrv) split = NULL;

	va_start (args, fmt);
	tmp = g_strdup_vprintf (fmt, args);
	va_end (args);

	split = g_strsplit (tmp, " ", -1);
	for (guint i = 0; split[i] != NULL; i++)
		g_ptr_array_add (argv, g_strdup (split[i]));
}

/**
 * fu_common_firmware_builder:
 * @bytes: The data to use
 * @script_fn: Name of the script to run in the tarball, e.g. `startup.sh`
 * @output_fn: Name of the generated firmware, e.g. `firmware.bin`
 * @error: A #GError, or %NULL
 *
 * Builds a firmware file using tools from the host session in a bubblewrap
 * jail. Several things happen during build:
 *
 * 1. The @bytes data is untarred to a temporary location
 * 2. A bubblewrap container is set up
 * 3. The startup.sh script is run inside the container
 * 4. The firmware.bin is extracted from the container
 * 5. The temporary location is deleted
 *
 * Returns: a new #GBytes, or %NULL for error
 **/
GBytes *
fu_common_firmware_builder (GBytes *bytes,
			    const gchar *script_fn,
			    const gchar *output_fn,
			    GError **error)
{
	gint rc = 0;
	g_autofree gchar *argv_str = NULL;
	g_autofree gchar *localstatedir = NULL;
	g_autofree gchar *output2_fn = NULL;
	g_autofree gchar *standard_error = NULL;
	g_autofree gchar *standard_output = NULL;
	g_autofree gchar *tmpdir = NULL;
	g_autoptr(GBytes) firmware_blob = NULL;
	g_autoptr(GPtrArray) argv = g_ptr_array_new_with_free_func (g_free);

	g_return_val_if_fail (bytes != NULL, NULL);
	g_return_val_if_fail (script_fn != NULL, NULL);
	g_return_val_if_fail (output_fn != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* untar file to temp location */
	tmpdir = g_dir_make_tmp ("fwupd-gen-XXXXXX", error);
	if (tmpdir == NULL)
		return NULL;
	if (!fu_common_extract_archive (bytes, tmpdir, error))
		return NULL;

	/* this is shared with the plugins */
	localstatedir = g_build_filename (LOCALSTATEDIR, "lib", "fwupd", "builder", NULL);

	/* launch bubblewrap and generate firmware */
	g_ptr_array_add (argv, g_strdup ("bwrap"));
	fu_common_add_argv (argv, "--die-with-parent");
	fu_common_add_argv (argv, "--ro-bind /usr /usr");
	fu_common_add_argv (argv, "--dir /tmp");
	fu_common_add_argv (argv, "--dir /var");
	fu_common_add_argv (argv, "--bind %s /tmp", tmpdir);
	if (g_file_test (localstatedir, G_FILE_TEST_EXISTS))
		fu_common_add_argv (argv, "--ro-bind %s /boot", localstatedir);
	fu_common_add_argv (argv, "--dev /dev");
	fu_common_add_argv (argv, "--symlink usr/lib /lib");
	fu_common_add_argv (argv, "--symlink usr/lib64 /lib64");
	fu_common_add_argv (argv, "--symlink usr/bin /bin");
	fu_common_add_argv (argv, "--symlink usr/sbin /sbin");
	fu_common_add_argv (argv, "--chdir /tmp");
	fu_common_add_argv (argv, "--unshare-all");
	fu_common_add_argv (argv, "/tmp/%s", script_fn);
	g_ptr_array_add (argv, NULL);
	argv_str = g_strjoinv (" ", (gchar **) argv->pdata);
	g_debug ("running '%s' in %s", argv_str, tmpdir);
	if (!g_spawn_sync ("/tmp",
			   (gchar **) argv->pdata,
			   NULL,
			   G_SPAWN_SEARCH_PATH,
			   NULL, NULL, /* child_setup */
			   &standard_output,
			   &standard_error,
			   &rc,
			   error)) {
		g_prefix_error (error, "failed to run '%s': ", argv_str);
		return NULL;
	}
	if (standard_output != NULL && standard_output[0] != '\0')
		g_debug ("console output was: %s", standard_output);
	if (rc != 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "failed to build firmware: %s",
			     standard_error);
		return NULL;
	}

	/* get generated file */
	output2_fn = g_build_filename (tmpdir, output_fn, NULL);
	firmware_blob = fu_common_get_contents_bytes (output2_fn, error);
	if (firmware_blob == NULL)
		return NULL;

	/* cleanup temp directory */
	if (!fu_common_rmtree (tmpdir, error))
		return NULL;

	/* success */
	return g_steal_pointer (&firmware_blob);
}

typedef struct {
	FuOutputHandler		 handler_cb;
	gpointer		 handler_user_data;
	GMainLoop		*loop;
	GSource			*source;
	GInputStream		*stream;
	GCancellable		*cancellable;
} FuCommonSpawnHelper;

static void fu_common_spawn_create_pollable_source (FuCommonSpawnHelper *helper);

static gboolean
fu_common_spawn_source_pollable_cb (GObject *stream, gpointer user_data)
{
	FuCommonSpawnHelper *helper = (FuCommonSpawnHelper *) user_data;
	gchar buffer[1024];
	gssize sz;
	g_auto(GStrv) split = NULL;
	g_autoptr(GError) error = NULL;

	/* read from stream */
	sz = g_pollable_input_stream_read_nonblocking (G_POLLABLE_INPUT_STREAM (stream),
						       buffer,
						       sizeof(buffer) - 1,
						       NULL,
						       &error);
	if (sz < 0) {
		if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK)) {
			g_warning ("failed to get read from nonblocking fd: %s",
				   error->message);
		}
		return G_SOURCE_REMOVE;
	}

	/* no read possible */
	if (sz == 0)
		g_main_loop_quit (helper->loop);

	/* emit lines */
	if (helper->handler_cb != NULL) {
		buffer[sz] = '\0';
		split = g_strsplit (buffer, "\n", -1);
		for (guint i = 0; split[i] != NULL; i++) {
			if (split[i][0] == '\0')
				continue;
			helper->handler_cb (split[i], helper->handler_user_data);
		}
	}

	/* set up the source for the next read */
	fu_common_spawn_create_pollable_source (helper);
	return G_SOURCE_REMOVE;
}

static void
fu_common_spawn_create_pollable_source (FuCommonSpawnHelper *helper)
{
	if (helper->source != NULL)
		g_source_destroy (helper->source);
	helper->source = g_pollable_input_stream_create_source (G_POLLABLE_INPUT_STREAM (helper->stream),
								helper->cancellable);
	g_source_attach (helper->source, NULL);
	g_source_set_callback (helper->source, (GSourceFunc) fu_common_spawn_source_pollable_cb, helper, NULL);
}

static void
fu_common_spawn_helper_free (FuCommonSpawnHelper *helper)
{
	if (helper->stream != NULL)
		g_object_unref (helper->stream);
	if (helper->source != NULL)
		g_source_destroy (helper->source);
	if (helper->loop != NULL)
		g_main_loop_unref (helper->loop);
	g_free (helper);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuCommonSpawnHelper, fu_common_spawn_helper_free)

/**
 * fu_common_spawn_sync:
 * @argv: The argument list to run
 * @handler_cb: (scope call): A #FuOutputHandler or %NULL
 * @handler_user_data: the user data to pass to @handler_cb
 * @cancellable: a #GCancellable, or %NULL
 * @error: A #GError or %NULL
 *
 * Runs a subprocess and waits for it to exit. Any output on standard out or
 * standard error will be forwarded to @handler_cb as whole lines.
 *
 * Returns: %TRUE for success
 **/
gboolean
fu_common_spawn_sync (const gchar * const * argv,
		      FuOutputHandler handler_cb,
		      gpointer handler_user_data,
		      GCancellable *cancellable, GError **error)
{
	g_autoptr(FuCommonSpawnHelper) helper = NULL;
	g_autoptr(GSubprocess) subprocess = NULL;
	g_autofree gchar *argv_str = NULL;

	/* create subprocess */
	argv_str = g_strjoinv (" ", (gchar **) argv);
	g_debug ("running '%s'", argv_str);
	subprocess = g_subprocess_newv (argv, G_SUBPROCESS_FLAGS_STDOUT_PIPE |
					      G_SUBPROCESS_FLAGS_STDERR_MERGE, error);
	if (subprocess == NULL)
		return FALSE;

	/* watch for process to exit */
	helper = g_new0 (FuCommonSpawnHelper, 1);
	helper->handler_cb = handler_cb;
	helper->handler_user_data = handler_user_data;
	helper->loop = g_main_loop_new (NULL, FALSE);
	helper->stream = g_subprocess_get_stdout_pipe (subprocess);
	helper->cancellable = cancellable;
	fu_common_spawn_create_pollable_source (helper);
	g_main_loop_run (helper->loop);
	return g_subprocess_wait_check (subprocess, cancellable, error);
}
