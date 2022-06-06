/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuSpawn"

#include "config.h"

#include <fwupdplugin.h>

#include <errno.h>
#ifdef HAVE_LIBARCHIVE
#include <archive.h>
#include <archive_entry.h>
#endif

#include "fu-firmware-builder.h"

#ifdef HAVE_LIBARCHIVE
static gboolean
fu_common_extract_archive_entry(struct archive_entry *entry, const gchar *dir)
{
	const gchar *tmp;
	g_autofree gchar *buf = NULL;

	/* no output file */
	if (archive_entry_pathname(entry) == NULL)
		return FALSE;

	/* update output path */
	tmp = archive_entry_pathname(entry);
	buf = g_build_filename(dir, tmp, NULL);
	archive_entry_update_pathname_utf8(entry, buf);
	return TRUE;
}
#endif

static gboolean
fu_common_extract_archive(GBytes *blob, const gchar *dir, GError **error)
{
#ifdef HAVE_LIBARCHIVE
	gboolean ret = TRUE;
	int r;
	struct archive *arch = NULL;
	struct archive_entry *entry;

	g_return_val_if_fail(blob != NULL, FALSE);
	g_return_val_if_fail(dir != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* decompress anything matching either glob */
	g_debug("decompressing into %s", dir);
	arch = archive_read_new();
	archive_read_support_format_all(arch);
	archive_read_support_filter_all(arch);
	r = archive_read_open_memory(arch,
				     (void *)g_bytes_get_data(blob, NULL),
				     (size_t)g_bytes_get_size(blob));
	if (r != 0) {
		ret = FALSE;
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "Cannot open: %s",
			    archive_error_string(arch));
		goto out;
	}
	for (;;) {
		gboolean valid;
		r = archive_read_next_header(arch, &entry);
		if (r == ARCHIVE_EOF)
			break;
		if (r != ARCHIVE_OK) {
			ret = FALSE;
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "Cannot read header: %s",
				    archive_error_string(arch));
			goto out;
		}

		/* only extract if valid */
		valid = fu_common_extract_archive_entry(entry, dir);
		if (!valid)
			continue;
		r = archive_read_extract(arch, entry, 0);
		if (r != ARCHIVE_OK) {
			ret = FALSE;
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "Cannot extract: %s",
				    archive_error_string(arch));
			goto out;
		}
	}
out:
	if (arch != NULL) {
		archive_read_close(arch);
		archive_read_free(arch);
	}
	return ret;
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "missing libarchive support");
	return FALSE;
#endif
}

static gboolean
fu_common_test_namespace_support(GError **error)
{
	/* test if CONFIG_USER_NS is valid */
	if (!g_file_test("/proc/self/ns/user", G_FILE_TEST_IS_SYMLINK)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "missing CONFIG_USER_NS in kernel");
		return FALSE;
	}
	if (g_file_test("/proc/sys/kernel/unprivileged_userns_clone", G_FILE_TEST_EXISTS)) {
		g_autofree gchar *clone = NULL;
		if (!g_file_get_contents("/proc/sys/kernel/unprivileged_userns_clone",
					 &clone,
					 NULL,
					 error))
			return FALSE;
		if (g_ascii_strtoll(clone, NULL, 10) == 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "unprivileged user namespace clones disabled by distro");
			return FALSE;
		}
	}
	return TRUE;
}

static void
fu_firmware_builder_add_argv(GPtrArray *argv, const gchar *fmt, ...) G_GNUC_PRINTF(2, 3);

static void
fu_firmware_builder_add_argv(GPtrArray *argv, const gchar *fmt, ...)
{
	va_list args;
	g_autofree gchar *tmp = NULL;
	g_auto(GStrv) split = NULL;

	va_start(args, fmt);
	tmp = g_strdup_vprintf(fmt, args);
	va_end(args);

	split = g_strsplit(tmp, " ", -1);
	for (guint i = 0; split[i] != NULL; i++)
		g_ptr_array_add(argv, g_strdup(split[i]));
}

/**
 * fu_firmware_builder_process:
 * @bytes: the data to use
 * @script_fn: Name of the script to run in the tarball, e.g. `startup.sh`
 * @output_fn: Name of the generated firmware, e.g. `firmware.bin`
 * @error: (nullable): optional return location for an error
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
 *
 * Since: 0.9.7
 **/
GBytes *
fu_firmware_builder_process(GBytes *bytes,
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
	g_autoptr(GPtrArray) argv = g_ptr_array_new_with_free_func(g_free);

	g_return_val_if_fail(bytes != NULL, NULL);
	g_return_val_if_fail(script_fn != NULL, NULL);
	g_return_val_if_fail(output_fn != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* find bwrap in the path */
	bwrap_fn = fu_path_find_program("bwrap", error);
	if (bwrap_fn == NULL)
		return NULL;

	/* test if CONFIG_USER_NS is valid */
	if (!fu_common_test_namespace_support(error))
		return NULL;

	/* untar file to temp location */
	tmpdir = g_dir_make_tmp("fwupd-gen-XXXXXX", error);
	if (tmpdir == NULL)
		return NULL;
	if (!fu_common_extract_archive(bytes, tmpdir, error))
		return NULL;

	/* this is shared with the plugins */
	localstatedir = fu_path_from_kind(FU_PATH_KIND_LOCALSTATEDIR_PKG);
	localstatebuilderdir = g_build_filename(localstatedir, "builder", NULL);

	/* launch bubblewrap and generate firmware */
	g_ptr_array_add(argv, g_steal_pointer(&bwrap_fn));
	fu_firmware_builder_add_argv(argv, "--die-with-parent");
	fu_firmware_builder_add_argv(argv, "--ro-bind /usr /usr");
	fu_firmware_builder_add_argv(argv, "--ro-bind /lib /lib");
	fu_firmware_builder_add_argv(argv, "--ro-bind-try /lib64 /lib64");
	fu_firmware_builder_add_argv(argv, "--ro-bind /bin /bin");
	fu_firmware_builder_add_argv(argv, "--ro-bind /sbin /sbin");
	fu_firmware_builder_add_argv(argv, "--dir /tmp");
	fu_firmware_builder_add_argv(argv, "--dir /var");
	fu_firmware_builder_add_argv(argv, "--bind %s /tmp", tmpdir);
	if (g_file_test(localstatebuilderdir, G_FILE_TEST_EXISTS))
		fu_firmware_builder_add_argv(argv, "--ro-bind %s /boot", localstatebuilderdir);
	fu_firmware_builder_add_argv(argv, "--dev /dev");
	fu_firmware_builder_add_argv(argv, "--chdir /tmp");
	fu_firmware_builder_add_argv(argv, "--unshare-all");
	fu_firmware_builder_add_argv(argv, "/tmp/%s", script_fn);
	g_ptr_array_add(argv, NULL);
	argv_str = g_strjoinv(" ", (gchar **)argv->pdata);
	g_debug("running '%s' in %s", argv_str, tmpdir);
	if (!g_spawn_sync("/tmp",
			  (gchar **)argv->pdata,
			  NULL,
			  G_SPAWN_SEARCH_PATH,
			  NULL,
			  NULL, /* child_setup */
			  &standard_output,
			  &standard_error,
			  &rc,
			  error)) {
		g_prefix_error(error, "failed to run '%s': ", argv_str);
		return NULL;
	}
	if (standard_output != NULL && standard_output[0] != '\0')
		g_debug("console output was: %s", standard_output);
	if (rc != 0) {
		FwupdError code = FWUPD_ERROR_INTERNAL;
		if (errno == ENOTTY)
			code = FWUPD_ERROR_PERMISSION_DENIED;
		g_set_error(error,
			    FWUPD_ERROR,
			    code,
			    "failed to build firmware: %s",
			    standard_error);
		return NULL;
	}

	/* get generated file */
	output2_fn = g_build_filename(tmpdir, output_fn, NULL);
	firmware_blob = fu_bytes_get_contents(output2_fn, error);
	if (firmware_blob == NULL)
		return NULL;

	/* cleanup temp directory */
	if (!fu_path_rmtree(tmpdir, error))
		return NULL;

	/* success */
	return g_steal_pointer(&firmware_blob);
}
