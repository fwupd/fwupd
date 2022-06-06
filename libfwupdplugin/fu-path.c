/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuCommon"

#include "config.h"

#include <errno.h>
#include <glib/gstdio.h>

#ifdef _WIN32
#include <stdlib.h>
#endif

#include "fwupd-error.h"

#include "fu-path-private.h"

/**
 * fu_path_rmtree:
 * @directory: a directory name
 * @error: (nullable): optional return location for an error
 *
 * Recursively removes a directory.
 *
 * Returns: %TRUE for success, %FALSE otherwise
 *
 * Since: 1.8.2
 **/
gboolean
fu_path_rmtree(const gchar *directory, GError **error)
{
	const gchar *filename;
	g_autoptr(GDir) dir = NULL;

	g_return_val_if_fail(directory != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* try to open */
	g_debug("removing %s", directory);
	dir = g_dir_open(directory, 0, error);
	if (dir == NULL)
		return FALSE;

	/* find each */
	while ((filename = g_dir_read_name(dir))) {
		g_autofree gchar *src = NULL;
		src = g_build_filename(directory, filename, NULL);
		if (g_file_test(src, G_FILE_TEST_IS_DIR)) {
			if (!fu_path_rmtree(src, error))
				return FALSE;
		} else {
			if (g_unlink(src) != 0) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INTERNAL,
					    "Failed to delete: %s",
					    src);
				return FALSE;
			}
		}
	}
	if (g_remove(directory) != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "Failed to delete: %s",
			    directory);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_path_get_file_list_internal(GPtrArray *files, const gchar *directory, GError **error)
{
	const gchar *filename;
	g_autoptr(GDir) dir = NULL;

	/* try to open */
	dir = g_dir_open(directory, 0, error);
	if (dir == NULL)
		return FALSE;

	/* find each */
	while ((filename = g_dir_read_name(dir))) {
		g_autofree gchar *src = g_build_filename(directory, filename, NULL);
		if (g_file_test(src, G_FILE_TEST_IS_DIR)) {
			if (!fu_path_get_file_list_internal(files, src, error))
				return FALSE;
		} else {
			g_ptr_array_add(files, g_steal_pointer(&src));
		}
	}
	return TRUE;
}

/**
 * fu_path_get_files:
 * @path: a directory name
 * @error: (nullable): optional return location for an error
 *
 * Returns every file found under @directory, and any subdirectory.
 * If any path under @directory cannot be accessed due to permissions an error
 * will be returned.
 *
 * Returns: (transfer container) (element-type utf8): array of files, or %NULL for error
 *
 * Since: 1.8.2
 **/
GPtrArray *
fu_path_get_files(const gchar *path, GError **error)
{
	g_autoptr(GPtrArray) files = g_ptr_array_new_with_free_func(g_free);

	g_return_val_if_fail(path != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	if (!fu_path_get_file_list_internal(files, path, error))
		return NULL;
	return g_steal_pointer(&files);
}

/**
 * fu_path_mkdir:
 * @dirname: a directory name
 * @error: (nullable): optional return location for an error
 *
 * Creates any required directories, including any parent directories.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.8.2
 **/
gboolean
fu_path_mkdir(const gchar *dirname, GError **error)
{
	g_return_val_if_fail(dirname != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (!g_file_test(dirname, G_FILE_TEST_IS_DIR))
		g_debug("creating path %s", dirname);
	if (g_mkdir_with_parents(dirname, 0755) == -1) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "Failed to create '%s': %s",
			    dirname,
			    g_strerror(errno));
		return FALSE;
	}
	return TRUE;
}

/**
 * fu_path_mkdir_parent:
 * @filename: a full pathname
 * @error: (nullable): optional return location for an error
 *
 * Creates any required directories, including any parent directories.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.8.2
 **/
gboolean
fu_path_mkdir_parent(const gchar *filename, GError **error)
{
	g_autofree gchar *parent = NULL;

	g_return_val_if_fail(filename != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	parent = g_path_get_dirname(filename);
	return fu_path_mkdir(parent, error);
}

/**
 * fu_path_find_program:
 * @basename: the program to search
 * @error: (nullable): optional return location for an error
 *
 * Looks for a program in the PATH variable
 *
 * Returns: a new #gchar, or %NULL for error
 *
 * Since: 1.8.2
 **/
gchar *
fu_path_find_program(const gchar *basename, GError **error)
{
	gchar *fn = g_find_program_in_path(basename);
	if (fn == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "missing executable %s in PATH",
			    basename);
		return NULL;
	}
	return fn;
}

/**
 * fu_path_get_win32_basedir:
 *
 * Gets the base directory that fwupd has been launched from on Windows.
 * This is the directory containing all subdirectories (IE 'C:\Program Files (x86)\fwupd\')
 *
 * Returns: The system path, or %NULL if invalid
 *
 * Since: 1.8.2
 **/
static gchar *
fu_path_get_win32_basedir(void)
{
#ifdef _WIN32
	char drive_buf[_MAX_DRIVE];
	char dir_buf[_MAX_DIR];
	_splitpath(_pgmptr, drive_buf, dir_buf, NULL, NULL);
	return g_build_filename(drive_buf, dir_buf, "..", NULL);
#endif
	return NULL;
}

/**
 * fu_path_from_kind:
 * @path_kind: a #FuPathKind e.g. %FU_PATH_KIND_DATADIR_PKG
 *
 * Gets a fwupd-specific system path. These can be overridden with various
 * environment variables, for instance %FWUPD_DATADIR.
 *
 * Returns: a system path, or %NULL if invalid
 *
 * Since: 1.8.2
 **/
gchar *
fu_path_from_kind(FuPathKind path_kind)
{
	const gchar *tmp;
	g_autofree gchar *basedir = NULL;

	switch (path_kind) {
	/* /var */
	case FU_PATH_KIND_LOCALSTATEDIR:
		tmp = g_getenv("FWUPD_LOCALSTATEDIR");
		if (tmp != NULL)
			return g_strdup(tmp);
#ifdef _WIN32
		return g_build_filename(g_getenv("USERPROFILE"),
					PACKAGE_NAME,
					FWUPD_LOCALSTATEDIR,
					NULL);
#else
		tmp = g_getenv("SNAP_USER_DATA");
		if (tmp != NULL)
			return g_build_filename(tmp, FWUPD_LOCALSTATEDIR, NULL);
		return g_build_filename(FWUPD_LOCALSTATEDIR, NULL);
#endif
	/* /proc */
	case FU_PATH_KIND_PROCFS:
		tmp = g_getenv("FWUPD_PROCFS");
		if (tmp != NULL)
			return g_strdup(tmp);
		return g_strdup("/proc");
	/* /sys/firmware */
	case FU_PATH_KIND_SYSFSDIR_FW:
		tmp = g_getenv("FWUPD_SYSFSFWDIR");
		if (tmp != NULL)
			return g_strdup(tmp);
		return g_strdup("/sys/firmware");
	/* /sys/class/tpm */
	case FU_PATH_KIND_SYSFSDIR_TPM:
		tmp = g_getenv("FWUPD_SYSFSTPMDIR");
		if (tmp != NULL)
			return g_strdup(tmp);
		return g_strdup("/sys/class/tpm");
	/* /sys/bus/platform/drivers */
	case FU_PATH_KIND_SYSFSDIR_DRIVERS:
		tmp = g_getenv("FWUPD_SYSFSDRIVERDIR");
		if (tmp != NULL)
			return g_strdup(tmp);
		return g_strdup("/sys/bus/platform/drivers");
	/* /sys/kernel/security */
	case FU_PATH_KIND_SYSFSDIR_SECURITY:
		tmp = g_getenv("FWUPD_SYSFSSECURITYDIR");
		if (tmp != NULL)
			return g_strdup(tmp);
		return g_strdup("/sys/kernel/security");
	/* /sys/firmware/acpi/tables */
	case FU_PATH_KIND_ACPI_TABLES:
		tmp = g_getenv("FWUPD_ACPITABLESDIR");
		if (tmp != NULL)
			return g_strdup(tmp);
		return g_strdup("/sys/firmware/acpi/tables");
	/* /sys/module/firmware_class/parameters/path */
	case FU_PATH_KIND_FIRMWARE_SEARCH:
		tmp = g_getenv("FWUPD_FIRMWARESEARCH");
		if (tmp != NULL)
			return g_strdup(tmp);
		return g_strdup("/sys/module/firmware_class/parameters/path");
	/* /etc */
	case FU_PATH_KIND_SYSCONFDIR:
		tmp = g_getenv("FWUPD_SYSCONFDIR");
		if (tmp != NULL)
			return g_strdup(tmp);
		tmp = g_getenv("SNAP_USER_DATA");
		if (tmp != NULL)
			return g_build_filename(tmp, FWUPD_SYSCONFDIR, NULL);
		basedir = fu_path_get_win32_basedir();
		if (basedir != NULL)
			return g_build_filename(basedir, FWUPD_SYSCONFDIR, NULL);
		return g_strdup(FWUPD_SYSCONFDIR);

	/* /usr/lib/<triplet>/fwupd-plugins-3 */
	case FU_PATH_KIND_PLUGINDIR_PKG:
		tmp = g_getenv("FWUPD_PLUGINDIR");
		if (tmp != NULL)
			return g_strdup(tmp);
		tmp = g_getenv("SNAP");
		if (tmp != NULL)
			return g_build_filename(tmp, FWUPD_PLUGINDIR, NULL);
		basedir = fu_path_get_win32_basedir();
		if (basedir != NULL)
			return g_build_filename(basedir, FWUPD_PLUGINDIR, NULL);
		return g_build_filename(FWUPD_PLUGINDIR, NULL);
	/* /usr/share/fwupd */
	case FU_PATH_KIND_DATADIR_PKG:
		tmp = g_getenv("FWUPD_DATADIR");
		if (tmp != NULL)
			return g_strdup(tmp);
		tmp = g_getenv("SNAP");
		if (tmp != NULL)
			return g_build_filename(tmp, FWUPD_DATADIR, PACKAGE_NAME, NULL);
		basedir = fu_path_get_win32_basedir();
		if (basedir != NULL)
			return g_build_filename(basedir, FWUPD_DATADIR, PACKAGE_NAME, NULL);
		return g_build_filename(FWUPD_DATADIR, PACKAGE_NAME, NULL);
	/* /usr/share/fwupd/quirks.d */
	case FU_PATH_KIND_DATADIR_QUIRKS:
		tmp = g_getenv("FWUPD_DATADIR_QUIRKS");
		if (tmp != NULL)
			return g_strdup(tmp);
		basedir = fu_path_from_kind(FU_PATH_KIND_DATADIR_PKG);
		return g_build_filename(basedir, "quirks.d", NULL);
	/* /usr/libexec/fwupd/efi */
	case FU_PATH_KIND_EFIAPPDIR:
		tmp = g_getenv("FWUPD_EFIAPPDIR");
		if (tmp != NULL)
			return g_strdup(tmp);
#ifdef EFI_APP_LOCATION
		tmp = g_getenv("SNAP");
		if (tmp != NULL)
			return g_build_filename(tmp, EFI_APP_LOCATION, NULL);
		return g_strdup(EFI_APP_LOCATION);
#else
		return NULL;
#endif
	/* /etc/fwupd */
	case FU_PATH_KIND_SYSCONFDIR_PKG:
		tmp = g_getenv("CONFIGURATION_DIRECTORY");
		if (tmp != NULL && g_file_test(tmp, G_FILE_TEST_EXISTS))
			return g_build_filename(tmp, NULL);
		basedir = fu_path_from_kind(FU_PATH_KIND_SYSCONFDIR);
		return g_build_filename(basedir, PACKAGE_NAME, NULL);
	/* /var/lib/fwupd */
	case FU_PATH_KIND_LOCALSTATEDIR_PKG:
		tmp = g_getenv("STATE_DIRECTORY");
		if (tmp != NULL && g_file_test(tmp, G_FILE_TEST_EXISTS))
			return g_build_filename(tmp, NULL);
		basedir = fu_path_from_kind(FU_PATH_KIND_LOCALSTATEDIR);
		return g_build_filename(basedir, "lib", PACKAGE_NAME, NULL);
	/* /var/lib/fwupd/quirks.d */
	case FU_PATH_KIND_LOCALSTATEDIR_QUIRKS:
		tmp = g_getenv("FWUPD_LOCALSTATEDIR_QUIRKS");
		if (tmp != NULL)
			return g_build_filename(tmp, NULL);
		basedir = fu_path_from_kind(FU_PATH_KIND_LOCALSTATEDIR_PKG);
		return g_build_filename(basedir, "quirks.d", NULL);
	/* /var/lib/fwupd/metadata */
	case FU_PATH_KIND_LOCALSTATEDIR_METADATA:
		tmp = g_getenv("FWUPD_LOCALSTATEDIR_METADATA");
		if (tmp != NULL)
			return g_build_filename(tmp, NULL);
		basedir = fu_path_from_kind(FU_PATH_KIND_LOCALSTATEDIR_PKG);
		return g_build_filename(basedir, "metadata", NULL);
	/* /var/lib/fwupd/remotes.d */
	case FU_PATH_KIND_LOCALSTATEDIR_REMOTES:
		tmp = g_getenv("FWUPD_LOCALSTATEDIR_REMOTES");
		if (tmp != NULL)
			return g_build_filename(tmp, NULL);
		basedir = fu_path_from_kind(FU_PATH_KIND_LOCALSTATEDIR_PKG);
		return g_build_filename(basedir, "remotes.d", NULL);
	/* /var/cache/fwupd */
	case FU_PATH_KIND_CACHEDIR_PKG:
		tmp = g_getenv("CACHE_DIRECTORY");
		if (tmp != NULL && g_file_test(tmp, G_FILE_TEST_EXISTS))
			return g_build_filename(tmp, NULL);
		basedir = fu_path_from_kind(FU_PATH_KIND_LOCALSTATEDIR);
		return g_build_filename(basedir, "cache", PACKAGE_NAME, NULL);
	/* /var/etc/fwupd */
	case FU_PATH_KIND_LOCALCONFDIR_PKG:
		tmp = g_getenv("LOCALCONF_DIRECTORY");
		if (tmp != NULL && g_file_test(tmp, G_FILE_TEST_EXISTS))
			return g_build_filename(tmp, NULL);
		basedir = fu_path_from_kind(FU_PATH_KIND_LOCALSTATEDIR);
		return g_build_filename(basedir, "etc", PACKAGE_NAME, NULL);
	/* /run/lock */
	case FU_PATH_KIND_LOCKDIR:
		return g_strdup("/run/lock");
	/* /sys/class/firmware-attributes */
	case FU_PATH_KIND_SYSFSDIR_FW_ATTRIB:
		tmp = g_getenv("FWUPD_SYSFSFWATTRIBDIR");
		if (tmp != NULL)
			return g_strdup(tmp);
		return g_strdup("/sys/class/firmware-attributes");
	case FU_PATH_KIND_OFFLINE_TRIGGER:
		tmp = g_getenv("FWUPD_OFFLINE_TRIGGER");
		if (tmp != NULL)
			return g_strdup(tmp);
		return g_strdup("/system-update");
	case FU_PATH_KIND_POLKIT_ACTIONS:
#ifdef POLKIT_ACTIONDIR
		return g_strdup(POLKIT_ACTIONDIR);
#else
		return NULL;
#endif
	/* C:\Program Files (x86)\fwupd\ */
	case FU_PATH_KIND_WIN32_BASEDIR:
		return fu_path_get_win32_basedir();
	/* this shouldn't happen */
	default:
		g_warning("cannot build path for unknown kind %u", path_kind);
	}

	return NULL;
}

/**
 * fu_path_fnmatch:
 * @pattern: a glob pattern, e.g. `*foo*`
 * @str: a string to match against the pattern, e.g. `bazfoobar`
 *
 * Matches a string against a glob pattern.
 *
 * Returns: %TRUE if the string matched
 *
 * Since: 1.8.2
 **/
gboolean
fu_path_fnmatch(const gchar *pattern, const gchar *str)
{
	g_return_val_if_fail(pattern != NULL, FALSE);
	g_return_val_if_fail(str != NULL, FALSE);
	return fu_path_fnmatch_impl(pattern, str);
}

static gint
fu_path_glob_sort_cb(gconstpointer a, gconstpointer b)
{
	return g_strcmp0(*(const gchar **)a, *(const gchar **)b);
}

/**
 * fu_path_glob:
 * @directory: a directory path
 * @pattern: a glob pattern, e.g. `*foo*`
 * @error: (nullable): optional return location for an error
 *
 * Returns all the filenames that match a specific glob pattern.
 * Any results are sorted. No matching files will set @error.
 *
 * Returns:  (element-type utf8) (transfer container): matching files, or %NULL
 *
 * Since: 1.8.2
 **/
GPtrArray *
fu_path_glob(const gchar *directory, const gchar *pattern, GError **error)
{
	const gchar *basename;
	g_autoptr(GDir) dir = NULL;
	g_autoptr(GPtrArray) files = g_ptr_array_new_with_free_func(g_free);

	g_return_val_if_fail(directory != NULL, NULL);
	g_return_val_if_fail(pattern != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	dir = g_dir_open(directory, 0, error);
	if (dir == NULL)
		return NULL;
	while ((basename = g_dir_read_name(dir)) != NULL) {
		if (!fu_path_fnmatch(pattern, basename))
			continue;
		g_ptr_array_add(files, g_build_filename(directory, basename, NULL));
	}
	if (files->len == 0) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_FOUND,
				    "no files matched pattern");
		return NULL;
	}
	g_ptr_array_sort(files, fu_path_glob_sort_cb);
	return g_steal_pointer(&files);
}
