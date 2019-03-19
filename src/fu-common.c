/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN				"FuCommon"

#include <config.h>

#include <gio/gunixinputstream.h>
#include <glib/gstdio.h>

#include <archive_entry.h>
#include <archive.h>
#include <errno.h>
#include <string.h>

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

static gboolean
fu_common_get_file_list_internal (GPtrArray *files, const gchar *directory, GError **error)
{
	const gchar *filename;
	g_autoptr(GDir) dir = NULL;

	/* try to open */
	dir = g_dir_open (directory, 0, error);
	if (dir == NULL)
		return FALSE;

	/* find each */
	while ((filename = g_dir_read_name (dir))) {
		g_autofree gchar *src = g_build_filename (directory, filename, NULL);
		if (g_file_test (src, G_FILE_TEST_IS_DIR)) {
			if (!fu_common_get_file_list_internal (files, src, error))
				return FALSE;
		} else {
			g_ptr_array_add (files, g_steal_pointer (&src));
		}
	}
	return TRUE;

}

/**
 * fu_common_get_files_recursive:
 * @path: a directory name
 * @error: A #GError or %NULL
 *
 * Returns every file found under @directory, and any subdirectory.
 * If any path under @directory cannot be accessed due to permissions an error
 * will be returned.
 *
 * Returns: (element-type: utf8) (transfer container): array of files, or %NULL for error
 **/
GPtrArray *
fu_common_get_files_recursive (const gchar *path, GError **error)
{
	g_autoptr(GPtrArray) files = g_ptr_array_new_with_free_func (g_free);
	if (!fu_common_get_file_list_internal (files, path, error))
		return NULL;
	return g_steal_pointer (&files);
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
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* this is invalid */
	if (count == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "A maximum read size must be specified");
		return NULL;
	}

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

gchar *
fu_common_find_program_in_path (const gchar *basename, GError **error)
{
	gchar *fn = g_find_program_in_path (basename);
	if (fn == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "missing executable %s in PATH",
			     basename);
		return NULL;
	}
	return fn;
}

static gboolean
fu_common_test_namespace_support (GError **error)
{
	/* test if CONFIG_USER_NS is valid */
	if (!g_file_test ("/proc/self/ns/user", G_FILE_TEST_IS_SYMLINK)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "missing CONFIG_USER_NS in kernel");
		return FALSE;
	}
	if (g_file_test ("/proc/sys/kernel/unprivileged_userns_clone", G_FILE_TEST_EXISTS)) {
		g_autofree gchar *clone = NULL;
		if (!g_file_get_contents ("/proc/sys/kernel/unprivileged_userns_clone", &clone, NULL, error))
			return FALSE;
		if (g_ascii_strtoll (clone, NULL, 10) == 0) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "unprivileged user namespace clones disabled by distro");
			return FALSE;
		}
	}
	return TRUE;
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
	g_autofree gchar *bwrap_fn = NULL;
	g_autofree gchar *localstatebuilderdir = NULL;
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

	/* find bwrap in the path */
	bwrap_fn = fu_common_find_program_in_path ("bwrap", error);
	if (bwrap_fn == NULL)
		return NULL;

	/* test if CONFIG_USER_NS is valid */
	if (!fu_common_test_namespace_support (error))
		return NULL;

	/* untar file to temp location */
	tmpdir = g_dir_make_tmp ("fwupd-gen-XXXXXX", error);
	if (tmpdir == NULL)
		return NULL;
	if (!fu_common_extract_archive (bytes, tmpdir, error))
		return NULL;

	/* this is shared with the plugins */
	localstatedir = fu_common_get_path (FU_PATH_KIND_LOCALSTATEDIR_PKG);
	localstatebuilderdir = g_build_filename (localstatedir, "builder", NULL);

	/* launch bubblewrap and generate firmware */
	g_ptr_array_add (argv, g_steal_pointer (&bwrap_fn));
	fu_common_add_argv (argv, "--die-with-parent");
	fu_common_add_argv (argv, "--ro-bind /usr /usr");
	fu_common_add_argv (argv, "--ro-bind /lib /lib");
	fu_common_add_argv (argv, "--ro-bind /lib64 /lib64");
	fu_common_add_argv (argv, "--ro-bind /bin /bin");
	fu_common_add_argv (argv, "--ro-bind /sbin /sbin");
	fu_common_add_argv (argv, "--dir /tmp");
	fu_common_add_argv (argv, "--dir /var");
	fu_common_add_argv (argv, "--bind %s /tmp", tmpdir);
	if (g_file_test (localstatebuilderdir, G_FILE_TEST_EXISTS))
		fu_common_add_argv (argv, "--ro-bind %s /boot", localstatebuilderdir);
	fu_common_add_argv (argv, "--dev /dev");
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
		FwupdError code = FWUPD_ERROR_INTERNAL;
		if (errno == ENOTTY)
			code = FWUPD_ERROR_PERMISSION_DENIED;
		g_set_error (error,
			     FWUPD_ERROR,
			     code,
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
	guint			 timeout_id;
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
	g_object_unref (helper->cancellable);
	if (helper->stream != NULL)
		g_object_unref (helper->stream);
	if (helper->source != NULL)
		g_source_destroy (helper->source);
	if (helper->loop != NULL)
		g_main_loop_unref (helper->loop);
	if (helper->timeout_id != 0)
		g_source_remove (helper->timeout_id);
	g_free (helper);
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuCommonSpawnHelper, fu_common_spawn_helper_free)
#pragma clang diagnostic pop

static gboolean
fu_common_spawn_timeout_cb (gpointer user_data)
{
	FuCommonSpawnHelper *helper = (FuCommonSpawnHelper *) user_data;
	g_cancellable_cancel (helper->cancellable);
	g_main_loop_quit (helper->loop);
	helper->timeout_id = 0;
	return G_SOURCE_REMOVE;
}

static void
fu_common_spawn_cancelled_cb (GCancellable *cancellable, FuCommonSpawnHelper *helper)
{
	/* just propagate */
	g_cancellable_cancel (helper->cancellable);
}

/**
 * fu_common_spawn_sync:
 * @argv: The argument list to run
 * @handler_cb: (scope call): A #FuOutputHandler or %NULL
 * @handler_user_data: the user data to pass to @handler_cb
 * @timeout_ms: a timeout in ms, or 0 for no limit
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
		      guint timeout_ms,
		      GCancellable *cancellable, GError **error)
{
	g_autoptr(FuCommonSpawnHelper) helper = NULL;
	g_autoptr(GSubprocess) subprocess = NULL;
	g_autofree gchar *argv_str = NULL;
	gulong cancellable_id = 0;

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

	/* always create a cancellable, and connect up the parent */
	helper->cancellable = g_cancellable_new ();
	if (cancellable != NULL) {
		cancellable_id = g_cancellable_connect (cancellable,
							G_CALLBACK (fu_common_spawn_cancelled_cb),
							helper, NULL);
	}

	/* allow timeout */
	if (timeout_ms > 0) {
		helper->timeout_id = g_timeout_add (timeout_ms,
						    fu_common_spawn_timeout_cb,
						    helper);
	}
	fu_common_spawn_create_pollable_source (helper);
	g_main_loop_run (helper->loop);
	g_cancellable_disconnect (cancellable, cancellable_id);
	if (g_cancellable_set_error_if_cancelled (helper->cancellable, error))
		return FALSE;
	return g_subprocess_wait_check (subprocess, cancellable, error);
}

/**
 * fu_common_write_uint16:
 * @buf: A writable buffer
 * @val_native: a value in host byte-order
 * @endian: A #FuEndianType, e.g. %G_LITTLE_ENDIAN
 *
 * Writes a value to a buffer using a specified endian.
 **/
void
fu_common_write_uint16 (guint8 *buf, guint16 val_native, FuEndianType endian)
{
	guint16 val_hw;
	switch (endian) {
	case G_BIG_ENDIAN:
		val_hw = GUINT16_TO_BE(val_native);
		break;
	case G_LITTLE_ENDIAN:
		val_hw = GUINT16_TO_LE(val_native);
		break;
	default:
		g_assert_not_reached ();
	}
	memcpy (buf, &val_hw, sizeof(val_hw));
}

/**
 * fu_common_write_uint32:
 * @buf: A writable buffer
 * @val_native: a value in host byte-order
 * @endian: A #FuEndianType, e.g. %G_LITTLE_ENDIAN
 *
 * Writes a value to a buffer using a specified endian.
 **/
void
fu_common_write_uint32 (guint8 *buf, guint32 val_native, FuEndianType endian)
{
	guint32 val_hw;
	switch (endian) {
	case G_BIG_ENDIAN:
		val_hw = GUINT32_TO_BE(val_native);
		break;
	case G_LITTLE_ENDIAN:
		val_hw = GUINT32_TO_LE(val_native);
		break;
	default:
		g_assert_not_reached ();
	}
	memcpy (buf, &val_hw, sizeof(val_hw));
}

/**
 * fu_common_read_uint16:
 * @buf: A readable buffer
 * @endian: A #FuEndianType, e.g. %G_LITTLE_ENDIAN
 *
 * Read a value from a buffer using a specified endian.
 *
 * Returns: a value in host byte-order
 **/
guint16
fu_common_read_uint16 (const guint8 *buf, FuEndianType endian)
{
	guint16 val_hw, val_native;
	memcpy (&val_hw, buf, sizeof(val_hw));
	switch (endian) {
	case G_BIG_ENDIAN:
		val_native = GUINT16_FROM_BE(val_hw);
		break;
	case G_LITTLE_ENDIAN:
		val_native = GUINT16_FROM_LE(val_hw);
		break;
	default:
		g_assert_not_reached ();
	}
	return val_native;
}

/**
 * fu_common_read_uint32:
 * @buf: A readable buffer
 * @endian: A #FuEndianType, e.g. %G_LITTLE_ENDIAN
 *
 * Read a value from a buffer using a specified endian.
 *
 * Returns: a value in host byte-order
 **/
guint32
fu_common_read_uint32 (const guint8 *buf, FuEndianType endian)
{
	guint32 val_hw, val_native;
	memcpy (&val_hw, buf, sizeof(val_hw));
	switch (endian) {
	case G_BIG_ENDIAN:
		val_native = GUINT32_FROM_BE(val_hw);
		break;
	case G_LITTLE_ENDIAN:
		val_native = GUINT32_FROM_LE(val_hw);
		break;
	default:
		g_assert_not_reached ();
	}
	return val_native;
}

/**
 * fu_common_strtoull:
 * @str: A string, e.g. "0x1234"
 *
 * Converts a string value to an integer. Values are assumed base 10, unless
 * prefixed with "0x" where they are parsed as base 16.
 *
 * Returns: integer value, or 0x0 for error
 **/
guint64
fu_common_strtoull (const gchar *str)
{
	guint base = 10;
	if (str == NULL)
		return 0x0;
	if (g_str_has_prefix (str, "0x")) {
		str += 2;
		base = 16;
	}
	return g_ascii_strtoull (str, NULL, base);
}

/**
 * fu_common_strstrip:
 * @str: A string, e.g. " test "
 *
 * Removes leading and trailing whitespace from a constant string.
 *
 * Returns: newly allocated string
 **/
gchar *
fu_common_strstrip (const gchar *str)
{
	guint head = G_MAXUINT;
	guint tail = 0;

	g_return_val_if_fail (str != NULL, NULL);

	/* find first non-space char */
	for (guint i = 0; str[i] != '\0'; i++) {
		if (str[i] != ' ') {
			head = i;
			break;
		}
	}
	if (head == G_MAXUINT)
		return g_strdup ("");

	/* find last non-space char */
	for (guint i = head; str[i] != '\0'; i++) {
		if (str[i] != ' ')
			tail = i;
	}
	return g_strndup (str + head, tail - head + 1);
}

static const GError *
fu_common_error_array_find (GPtrArray *errors, FwupdError error_code)
{
	for (guint j = 0; j < errors->len; j++) {
		const GError *error = g_ptr_array_index (errors, j);
		if (g_error_matches (error, FWUPD_ERROR, error_code))
			return error;
	}
	return NULL;
}

static guint
fu_common_error_array_count (GPtrArray *errors, FwupdError error_code)
{
	guint cnt = 0;
	for (guint j = 0; j < errors->len; j++) {
		const GError *error = g_ptr_array_index (errors, j);
		if (g_error_matches (error, FWUPD_ERROR, error_code))
			cnt++;
	}
	return cnt;
}

static gboolean
fu_common_error_array_matches_any (GPtrArray *errors, FwupdError *error_codes)
{
	for (guint j = 0; j < errors->len; j++) {
		const GError *error = g_ptr_array_index (errors, j);
		gboolean matches_any = FALSE;
		for (guint i = 0; error_codes[i] != FWUPD_ERROR_LAST; i++) {
			if (g_error_matches (error, FWUPD_ERROR, error_codes[i])) {
				matches_any = TRUE;
				break;
			}
		}
		if (!matches_any)
			return FALSE;
	}
	return TRUE;
}

/**
 * fu_common_error_array_get_best:
 * @errors: (element-type GError): array of errors
 *
 * Finds the 'best' error to show the user from a array of errors, creating a
 * completely bespoke error where required.
 *
 * Returns: (transfer full): a #GError, never %NULL
 **/
GError *
fu_common_error_array_get_best (GPtrArray *errors)
{
	FwupdError err_prio[] =		{ FWUPD_ERROR_INVALID_FILE,
					  FWUPD_ERROR_VERSION_SAME,
					  FWUPD_ERROR_VERSION_NEWER,
					  FWUPD_ERROR_NOT_SUPPORTED,
					  FWUPD_ERROR_INTERNAL,
					  FWUPD_ERROR_NOT_FOUND,
					  FWUPD_ERROR_LAST };
	FwupdError err_all_uptodate[] =	{ FWUPD_ERROR_VERSION_SAME,
					  FWUPD_ERROR_NOT_FOUND,
					  FWUPD_ERROR_NOT_SUPPORTED,
					  FWUPD_ERROR_LAST };
	FwupdError err_all_newer[] =	{ FWUPD_ERROR_VERSION_NEWER,
					  FWUPD_ERROR_VERSION_SAME,
					  FWUPD_ERROR_NOT_FOUND,
					  FWUPD_ERROR_NOT_SUPPORTED,
					  FWUPD_ERROR_LAST };

	/* are all the errors either GUID-not-matched or version-same? */
	if (fu_common_error_array_count (errors, FWUPD_ERROR_VERSION_SAME) > 1 &&
	    fu_common_error_array_matches_any (errors, err_all_uptodate)) {
		return g_error_new (FWUPD_ERROR,
				    FWUPD_ERROR_NOTHING_TO_DO,
				    "All updatable firmware is already installed");
	}

	/* are all the errors either GUID-not-matched or version same or newer? */
	if (fu_common_error_array_count (errors, FWUPD_ERROR_VERSION_NEWER) > 1 &&
	    fu_common_error_array_matches_any (errors, err_all_newer)) {
		return g_error_new (FWUPD_ERROR,
				    FWUPD_ERROR_NOTHING_TO_DO,
				    "All updatable devices already have newer versions");
	}

	/* get the most important single error */
	for (guint i = 0; err_prio[i] != FWUPD_ERROR_LAST; i++) {
		const GError *error_tmp = fu_common_error_array_find (errors, err_prio[i]);
		if (error_tmp != NULL)
			return g_error_copy (error_tmp);
	}

	/* fall back to something */
	return g_error_new (FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "No supported devices found");
}

/**
 * fu_common_get_path:
 * @path_kind: A #FuPathKind e.g. %FU_PATH_KIND_DATADIR_PKG
 *
 * Gets a fwupd-specific system path. These can be overridden with various
 * environment variables, for instance %FWUPD_DATADIR.
 *
 * Returns: a system path, or %NULL if invalid
 **/
gchar *
fu_common_get_path (FuPathKind path_kind)
{
	const gchar *tmp;
	g_autofree gchar *basedir = NULL;

	switch (path_kind) {
	/* /var */
	case FU_PATH_KIND_LOCALSTATEDIR:
		tmp = g_getenv ("FWUPD_LOCALSTATEDIR");
		if (tmp != NULL)
			return g_strdup (tmp);
		tmp = g_getenv ("SNAP_USER_DATA");
		if (tmp != NULL)
			return g_build_filename (tmp, LOCALSTATEDIR, NULL);
		return g_build_filename (LOCALSTATEDIR, NULL);
	/* /sys/firmware */
	case FU_PATH_KIND_SYSFSDIR_FW:
		tmp = g_getenv ("FWUPD_SYSFSFWDIR");
		if (tmp != NULL)
			return g_strdup (tmp);
		return g_strdup ("/sys/firmware");
	/* /sys/firmware */
	case FU_PATH_KIND_SYSFSDIR_TPM:
		tmp = g_getenv ("FWUPD_SYSFSTPMDIR");
		if (tmp != NULL)
			return g_strdup (tmp);
		return g_strdup ("/sys/class/tpm");
	/* /sys/bus/platform/drivers */
	case FU_PATH_KIND_SYSFSDIR_DRIVERS:
		tmp = g_getenv ("FWUPD_SYSFSDRIVERDIR");
		if (tmp != NULL)
			return g_strdup (tmp);
		return g_strdup ("/sys/bus/platform/drivers");
	/* /etc */
	case FU_PATH_KIND_SYSCONFDIR:
		tmp = g_getenv ("FWUPD_SYSCONFDIR");
		if (tmp != NULL)
			return g_strdup (tmp);
		tmp = g_getenv ("SNAP_USER_DATA");
		if (tmp != NULL)
			return g_build_filename (tmp, SYSCONFDIR, NULL);
		return g_strdup (SYSCONFDIR);
	/* /usr/lib/<triplet>/fwupd-plugins-3 */
	case FU_PATH_KIND_PLUGINDIR_PKG:
		tmp = g_getenv ("FWUPD_PLUGINDIR");
		if (tmp != NULL)
			return g_strdup (tmp);
		tmp = g_getenv ("SNAP");
		if (tmp != NULL)
			return g_build_filename (tmp, PLUGINDIR, NULL);
		return g_build_filename (PLUGINDIR, NULL);
	/* /usr/share/fwupd */
	case FU_PATH_KIND_DATADIR_PKG:
		tmp = g_getenv ("FWUPD_DATADIR");
		if (tmp != NULL)
			return g_strdup (tmp);
		tmp = g_getenv ("SNAP");
		if (tmp != NULL)
			return g_build_filename (tmp, DATADIR, PACKAGE_NAME, NULL);
		return g_build_filename (DATADIR, PACKAGE_NAME, NULL);
	/* /usr/libexec/fwupd/efi */
	case FU_PATH_KIND_EFIAPPDIR:
		tmp = g_getenv ("FWUPD_EFIAPPDIR");
		if (tmp != NULL)
			return g_strdup (tmp);
#ifdef EFI_APP_LOCATION
		tmp = g_getenv ("SNAP");
		if (tmp != NULL)
			return g_build_filename (tmp, EFI_APP_LOCATION, NULL);
		return g_strdup (EFI_APP_LOCATION);
#else
		return NULL;
#endif
	/* /etc/fwupd */
	case FU_PATH_KIND_SYSCONFDIR_PKG:
		basedir = fu_common_get_path (FU_PATH_KIND_SYSCONFDIR);
		return g_build_filename (basedir, PACKAGE_NAME, NULL);
	/* /var/lib/fwupd */
	case FU_PATH_KIND_LOCALSTATEDIR_PKG:
		basedir = fu_common_get_path (FU_PATH_KIND_LOCALSTATEDIR);
		return g_build_filename (basedir, "lib", PACKAGE_NAME, NULL);
	/* /var/cache/fwupd */
	case FU_PATH_KIND_CACHEDIR_PKG:
		basedir = fu_common_get_path (FU_PATH_KIND_LOCALSTATEDIR);
		return g_build_filename (basedir, "cache", PACKAGE_NAME, NULL);
	/* this shouldn't happen */
	default:
		g_warning ("cannot build path for unknown kind %u", path_kind);
	}

	return NULL;
}

/**
 * fu_common_string_replace:
 * @string: The #GString to operate on
 * @search: The text to search for
 * @replace: The text to use for substitutions
 *
 * Performs multiple search and replace operations on the given string.
 *
 * Returns: the number of replacements done, or 0 if @search is not found.
 *
 * Since: 1.2.0
 **/
guint
fu_common_string_replace (GString *string, const gchar *search, const gchar *replace)
{
	gchar *tmp;
	guint count = 0;
	gsize search_idx = 0;
	gsize replace_len;
	gsize search_len;

	g_return_val_if_fail (string != NULL, 0);
	g_return_val_if_fail (search != NULL, 0);
	g_return_val_if_fail (replace != NULL, 0);

	/* nothing to do */
	if (string->len == 0)
		return 0;

	search_len = strlen (search);
	replace_len = strlen (replace);

	do {
		tmp = g_strstr_len (string->str + search_idx, -1, search);
		if (tmp == NULL)
			break;

		/* advance the counter in case @replace contains @search */
		search_idx = (gsize) (tmp - string->str);

		/* reallocate the string if required */
		if (search_len > replace_len) {
			g_string_erase (string,
					(gssize) search_idx,
					(gssize) (search_len - replace_len));
			memcpy (tmp, replace, replace_len);
		} else if (search_len < replace_len) {
			g_string_insert_len (string,
					     (gssize) search_idx,
					     replace,
					     (gssize) (replace_len - search_len));
			/* we have to treat this specially as it could have
			 * been reallocated when the insertion happened */
			memcpy (string->str + search_idx, replace, replace_len);
		} else {
			/* just memcmp in the new string */
			memcpy (tmp, replace, replace_len);
		}
		search_idx += replace_len;
		count++;
	} while (TRUE);

	return count;
}

/**
 * fu_common_dump_full:
 * @log_domain: log domain, typically %G_LOG_DOMAIN or %NULL
 * @title: prefix title, or %NULL
 * @data: buffer to print
 * @len: the size of @data
 * @columns: break new lines after this many bytes
 * @flags: some #FuDumpFlags, e.g. %FU_DUMP_FLAGS_SHOW_ASCII
 *
 * Dumps a raw buffer to the screen.
 *
 * Since: 1.2.4
 **/
void
fu_common_dump_full (const gchar *log_domain,
		     const gchar *title,
		     const guint8 *data,
		     gsize len,
		     guint columns,
		     FuDumpFlags flags)
{
	g_autoptr(GString) str = g_string_new (NULL);

	/* optional */
	if (title != NULL)
		g_string_append_printf (str, "%s:", title);

	/* if more than can fit on one line then start afresh */
	if (len > columns || flags & FU_DUMP_FLAGS_SHOW_ADDRESSES) {
		g_string_append (str, "\n");
	} else {
		for (gsize i = str->len; i < 16; i++)
			g_string_append (str, " ");
	}

	/* offset line */
	if (flags & FU_DUMP_FLAGS_SHOW_ADDRESSES) {
		g_string_append (str, "       │ ");
		for (gsize i = 0; i < columns; i++)
			g_string_append_printf (str, "%02x ", (guint) i);
		g_string_append (str, "\n───────┼");
		for (gsize i = 0; i < columns; i++)
			g_string_append (str, "───");
		g_string_append_printf (str, "\n0x%04x │ ", (guint) 0);
	}

	/* print each row */
	for (gsize i = 0; i < len; i++) {
		g_string_append_printf (str, "%02x ", data[i]);

		/* optionally print ASCII char */
		if (flags & FU_DUMP_FLAGS_SHOW_ASCII) {
			if (g_ascii_isprint (data[i]))
				g_string_append_printf (str, "[%c] ", data[i]);
			else
				g_string_append (str, "[?] ");
		}

		/* new row required */
		if (i > 0 && i != len - 1 && (i + 1) % columns == 0) {
			g_string_append (str, "\n");
			if (flags & FU_DUMP_FLAGS_SHOW_ADDRESSES)
				g_string_append_printf (str, "0x%04x │ ", (guint) i + 1);
		}
	}
	g_log (log_domain, G_LOG_LEVEL_DEBUG, "%s", str->str);
}

/**
 * fu_common_dump_raw:
 * @log_domain: log domain, typically %G_LOG_DOMAIN or %NULL
 * @title: prefix title, or %NULL
 * @data: buffer to print
 * @len: the size of @data
 *
 * Dumps a raw buffer to the screen.
 *
 * Since: 1.2.2
 **/
void
fu_common_dump_raw (const gchar *log_domain,
		    const gchar *title,
		    const guint8 *data,
		    gsize len)
{
	FuDumpFlags flags = FU_DUMP_FLAGS_NONE;
	if (len > 64)
		flags |= FU_DUMP_FLAGS_SHOW_ADDRESSES;
	fu_common_dump_full (log_domain, title, data, len, 32, flags);
}

/**
 * fu_common_dump_raw:
 * @log_domain: log domain, typically %G_LOG_DOMAIN or %NULL
 * @title: prefix title, or %NULL
 * @bytes: a #GBytes
 *
 * Dumps a byte buffer to the screen.
 *
 * Since: 1.2.2
 **/
void
fu_common_dump_bytes (const gchar *log_domain,
		      const gchar *title,
		      GBytes *bytes)
{
	gsize len = 0;
	const guint8 *data = g_bytes_get_data (bytes, &len);
	fu_common_dump_raw (log_domain, title, data, len);
}

/**
 * fu_common_bytes_align:
 * @bytes: a #GBytes
 * @blksz: block size in bytes
 * @padval: the byte used to pad the byte buffer
 *
 * Aligns a block of memory to @blksize using the @padval value; if
 * the block is already aligned then the original @bytes is returned.
 *
 * Returns: (transfer full): a #GBytes, possibly @bytes
 *
 * Since: 1.2.4
 **/
GBytes *
fu_common_bytes_align (GBytes *bytes, gsize blksz, gchar padval)
{
	const guint8 *data;
	gsize sz;

	g_return_val_if_fail (bytes != NULL, NULL);
	g_return_val_if_fail (blksz > 0, NULL);

	/* pad */
	data = g_bytes_get_data (bytes, &sz);
	if (sz % blksz != 0) {
		gsize sz_align = ((sz / blksz) + 1) * blksz;
		guint8 *data_align = g_malloc (sz_align);
		memcpy (data_align, data, sz);
		memset (data_align + sz, padval, sz_align - sz);
		g_debug ("aligning 0x%x bytes to 0x%x",
			 (guint) sz, (guint) sz_align);
		return g_bytes_new_take (data_align, sz_align);
	}

	/* perfectly aligned */
	return g_bytes_ref (bytes);
}

/**
 * fu_common_bytes_is_empty:
 * @bytes: a #GBytes
 *
 * Checks if a byte array are just empty (0xff) bytes.
 *
 * Return value: %TRUE if @bytes is empty
 **/
gboolean
fu_common_bytes_is_empty (GBytes *bytes)
{
	gsize sz = 0;
	const guint8 *buf = g_bytes_get_data (bytes, &sz);
	for (gsize i = 0; i < sz; i++) {
		if (buf[i] != 0xff)
			return FALSE;
	}
	return TRUE;
}
