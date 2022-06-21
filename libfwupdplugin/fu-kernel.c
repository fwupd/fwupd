/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuCommon"

#include "config.h"

#include <errno.h>
#include <glib/gstdio.h>
#ifdef HAVE_UTSNAME_H
#include <sys/utsname.h>
#endif

#include "fu-common.h"
#include "fu-kernel.h"
#include "fu-path.h"
#include "fu-version-common.h"

/**
 * fu_kernel_locked_down:
 *
 * Determines if kernel lockdown in effect
 *
 * Since: 1.8.2
 **/
gboolean
fu_kernel_locked_down(void)
{
#ifdef __linux__
	gsize len = 0;
	g_autofree gchar *dir = fu_path_from_kind(FU_PATH_KIND_SYSFSDIR_SECURITY);
	g_autofree gchar *fname = g_build_filename(dir, "lockdown", NULL);
	g_autofree gchar *data = NULL;
	g_auto(GStrv) options = NULL;

	if (!g_file_test(fname, G_FILE_TEST_EXISTS))
		return FALSE;
	if (!g_file_get_contents(fname, &data, &len, NULL))
		return FALSE;
	if (len < 1)
		return FALSE;
	options = g_strsplit(data, " ", -1);
	for (guint i = 0; options[i] != NULL; i++) {
		if (g_strcmp0(options[i], "[none]") == 0)
			return FALSE;
	}
	return TRUE;
#else
	return FALSE;
#endif
}

/**
 * fu_kernel_check_version:
 * @minimum_kernel: (not nullable): The minimum kernel version to check against
 * @error: (nullable): optional return location for an error
 *
 * Determines if the system is running at least a certain required kernel version
 *
 * Since: 1.8.2
 **/
gboolean
fu_kernel_check_version(const gchar *minimum_kernel, GError **error)
{
#ifdef HAVE_UTSNAME_H
	struct utsname name_tmp;

	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail(minimum_kernel != NULL, FALSE);

	memset(&name_tmp, 0, sizeof(struct utsname));
	if (uname(&name_tmp) < 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "failed to read kernel version");
		return FALSE;
	}
	if (fu_version_compare(name_tmp.release, minimum_kernel, FWUPD_VERSION_FORMAT_TRIPLET) <
	    0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "kernel %s doesn't meet minimum %s",
			    name_tmp.release,
			    minimum_kernel);
		return FALSE;
	}

	return TRUE;
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "platform doesn't support checking for minimum Linux kernel");
	return FALSE;
#endif
}

/**
 * fu_kernel_get_firmware_search_path:
 * @error: (nullable): optional return location for an error
 *
 * Reads the FU_PATH_KIND_FIRMWARE_SEARCH and
 * returns its contents
 *
 * Returns: a pointer to a gchar array
 *
 * Since: 1.8.2
 **/
gchar *
fu_kernel_get_firmware_search_path(GError **error)
{
	gsize sz = 0;
	g_autofree gchar *sys_fw_search_path = NULL;
	g_autofree gchar *contents = NULL;

	sys_fw_search_path = fu_path_from_kind(FU_PATH_KIND_FIRMWARE_SEARCH);
	if (!g_file_get_contents(sys_fw_search_path, &contents, &sz, error))
		return NULL;

	/* remove newline character */
	if (contents != NULL && sz > 0 && contents[sz - 1] == '\n')
		contents[sz - 1] = 0;

	g_debug("read firmware search path (%" G_GSIZE_FORMAT "): %s", sz, contents);

	return g_steal_pointer(&contents);
}

/**
 * fu_kernel_set_firmware_search_path:
 * @path: NUL-terminated string
 * @error: (nullable): optional return location for an error
 *
 * Writes path to the FU_PATH_KIND_FIRMWARE_SEARCH
 *
 * Returns: %TRUE if successful
 *
 * Since: 1.8.2
 **/
gboolean
fu_kernel_set_firmware_search_path(const gchar *path, GError **error)
{
#if GLIB_CHECK_VERSION(2, 66, 0)
	g_autofree gchar *sys_fw_search_path_prm = NULL;

	g_return_val_if_fail(path != NULL, FALSE);
	g_return_val_if_fail(strlen(path) < PATH_MAX, FALSE);

	sys_fw_search_path_prm = fu_path_from_kind(FU_PATH_KIND_FIRMWARE_SEARCH);

	g_debug("writing firmware search path (%" G_GSIZE_FORMAT "): %s", strlen(path), path);

	return g_file_set_contents_full(sys_fw_search_path_prm,
					path,
					strlen(path),
					G_FILE_SET_CONTENTS_NONE,
					0644,
					error);
#else
	FILE *fd;
	gsize res;
	g_autofree gchar *sys_fw_search_path_prm = NULL;

	g_return_val_if_fail(path != NULL, FALSE);
	g_return_val_if_fail(strlen(path) < PATH_MAX, FALSE);

	sys_fw_search_path_prm = fu_path_from_kind(FU_PATH_KIND_FIRMWARE_SEARCH);
	/* g_file_set_contents will try to create backup files in sysfs, so use fopen here */
	fd = fopen(sys_fw_search_path_prm, "w");
	if (fd == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_PERMISSION_DENIED,
			    "Failed to open %s: %s",
			    sys_fw_search_path_prm,
			    g_strerror(errno));
		return FALSE;
	}

	g_debug("writing firmware search path (%" G_GSIZE_FORMAT "): %s", strlen(path), path);

	res = fwrite(path, sizeof(gchar), strlen(path), fd);

	fclose(fd);

	if (res != strlen(path)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "Failed to write firmware search path: %s",
			    g_strerror(errno));
		return FALSE;
	}

	return TRUE;
#endif
}

/**
 * fu_kernel_reset_firmware_search_path:
 * @error: (nullable): optional return location for an error
 *
 * Resets the FU_PATH_KIND_FIRMWARE_SEARCH to an empty string
 *
 * Returns: %TRUE if successful
 *
 * Since: 1.8.2
 **/
gboolean
fu_kernel_reset_firmware_search_path(GError **error)
{
	const gchar *contents = " ";

	return fu_kernel_set_firmware_search_path(contents, error);
}
