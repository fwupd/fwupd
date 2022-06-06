/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuCommon"

#include <config.h>
#include <glib/gstdio.h>

#ifdef HAVE_KENV_H
#include <kenv.h>
#endif

#ifdef HAVE_CPUID_H
#include <cpuid.h>
#endif

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "fwupd-error.h"

#include "fu-bytes.h"
#include "fu-common-private.h"
#include "fu-common-version.h"
#include "fu-firmware.h"
#include "fu-string.h"
#include "fu-volume-private.h"

/**
 * fu_common_rmtree:
 * @directory: a directory name
 * @error: (nullable): optional return location for an error
 *
 * Recursively removes a directory.
 *
 * Returns: %TRUE for success, %FALSE otherwise
 *
 * Since: 0.9.7
 **/
gboolean
fu_common_rmtree(const gchar *directory, GError **error)
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
			if (!fu_common_rmtree(src, error))
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
fu_common_get_file_list_internal(GPtrArray *files, const gchar *directory, GError **error)
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
			if (!fu_common_get_file_list_internal(files, src, error))
				return FALSE;
		} else {
			g_ptr_array_add(files, g_steal_pointer(&src));
		}
	}
	return TRUE;
}

/**
 * fu_common_get_files_recursive:
 * @path: a directory name
 * @error: (nullable): optional return location for an error
 *
 * Returns every file found under @directory, and any subdirectory.
 * If any path under @directory cannot be accessed due to permissions an error
 * will be returned.
 *
 * Returns: (transfer container) (element-type utf8): array of files, or %NULL for error
 *
 * Since: 1.0.6
 **/
GPtrArray *
fu_common_get_files_recursive(const gchar *path, GError **error)
{
	g_autoptr(GPtrArray) files = g_ptr_array_new_with_free_func(g_free);

	g_return_val_if_fail(path != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	if (!fu_common_get_file_list_internal(files, path, error))
		return NULL;
	return g_steal_pointer(&files);
}

/**
 * fu_common_mkdir:
 * @dirname: a directory name
 * @error: (nullable): optional return location for an error
 *
 * Creates any required directories, including any parent directories.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.7.1
 **/
gboolean
fu_common_mkdir(const gchar *dirname, GError **error)
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
 * fu_common_mkdir_parent:
 * @filename: a full pathname
 * @error: (nullable): optional return location for an error
 *
 * Creates any required directories, including any parent directories.
 *
 * Returns: %TRUE for success
 *
 * Since: 0.9.7
 **/
gboolean
fu_common_mkdir_parent(const gchar *filename, GError **error)
{
	g_autofree gchar *parent = NULL;

	g_return_val_if_fail(filename != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	parent = g_path_get_dirname(filename);
	return fu_common_mkdir(parent, error);
}

/**
 * fu_common_find_program_in_path:
 * @basename: the program to search
 * @error: (nullable): optional return location for an error
 *
 * Looks for a program in the PATH variable
 *
 * Returns: a new #gchar, or %NULL for error
 *
 * Since: 1.1.2
 **/
gchar *
fu_common_find_program_in_path(const gchar *basename, GError **error)
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

static const GError *
fu_common_error_array_find(GPtrArray *errors, FwupdError error_code)
{
	for (guint j = 0; j < errors->len; j++) {
		const GError *error = g_ptr_array_index(errors, j);
		if (g_error_matches(error, FWUPD_ERROR, error_code))
			return error;
	}
	return NULL;
}

static guint
fu_common_error_array_count(GPtrArray *errors, FwupdError error_code)
{
	guint cnt = 0;
	for (guint j = 0; j < errors->len; j++) {
		const GError *error = g_ptr_array_index(errors, j);
		if (g_error_matches(error, FWUPD_ERROR, error_code))
			cnt++;
	}
	return cnt;
}

static gboolean
fu_common_error_array_matches_any(GPtrArray *errors, FwupdError *error_codes)
{
	for (guint j = 0; j < errors->len; j++) {
		const GError *error = g_ptr_array_index(errors, j);
		gboolean matches_any = FALSE;
		for (guint i = 0; error_codes[i] != FWUPD_ERROR_LAST; i++) {
			if (g_error_matches(error, FWUPD_ERROR, error_codes[i])) {
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
 *
 * Since: 1.0.8
 **/
GError *
fu_common_error_array_get_best(GPtrArray *errors)
{
	FwupdError err_prio[] = {FWUPD_ERROR_INVALID_FILE,
				 FWUPD_ERROR_VERSION_SAME,
				 FWUPD_ERROR_VERSION_NEWER,
				 FWUPD_ERROR_NOT_SUPPORTED,
				 FWUPD_ERROR_INTERNAL,
				 FWUPD_ERROR_NOT_FOUND,
				 FWUPD_ERROR_LAST};
	FwupdError err_all_uptodate[] = {FWUPD_ERROR_VERSION_SAME,
					 FWUPD_ERROR_NOT_FOUND,
					 FWUPD_ERROR_NOT_SUPPORTED,
					 FWUPD_ERROR_LAST};
	FwupdError err_all_newer[] = {FWUPD_ERROR_VERSION_NEWER,
				      FWUPD_ERROR_VERSION_SAME,
				      FWUPD_ERROR_NOT_FOUND,
				      FWUPD_ERROR_NOT_SUPPORTED,
				      FWUPD_ERROR_LAST};

	/* are all the errors either GUID-not-matched or version-same? */
	if (fu_common_error_array_count(errors, FWUPD_ERROR_VERSION_SAME) > 1 &&
	    fu_common_error_array_matches_any(errors, err_all_uptodate)) {
		return g_error_new(FWUPD_ERROR,
				   FWUPD_ERROR_NOTHING_TO_DO,
				   "All updatable firmware is already installed");
	}

	/* are all the errors either GUID-not-matched or version same or newer? */
	if (fu_common_error_array_count(errors, FWUPD_ERROR_VERSION_NEWER) > 1 &&
	    fu_common_error_array_matches_any(errors, err_all_newer)) {
		return g_error_new(FWUPD_ERROR,
				   FWUPD_ERROR_NOTHING_TO_DO,
				   "All updatable devices already have newer versions");
	}

	/* get the most important single error */
	for (guint i = 0; err_prio[i] != FWUPD_ERROR_LAST; i++) {
		const GError *error_tmp = fu_common_error_array_find(errors, err_prio[i]);
		if (error_tmp != NULL)
			return g_error_copy(error_tmp);
	}

	/* fall back to something */
	return g_error_new(FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, "No supported devices found");
}

/**
 * fu_common_get_win32_basedir:
 *
 * Gets the base directory that fwupd has been launched from on Windows.
 * This is the directory containing all subdirectories (IE 'C:\Program Files (x86)\fwupd\')
 *
 * Returns: The system path, or %NULL if invalid
 *
 * Since: 1.7.4
 **/
static gchar *
fu_common_get_win32_basedir(void)
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
 * fu_common_get_path:
 * @path_kind: a #FuPathKind e.g. %FU_PATH_KIND_DATADIR_PKG
 *
 * Gets a fwupd-specific system path. These can be overridden with various
 * environment variables, for instance %FWUPD_DATADIR.
 *
 * Returns: a system path, or %NULL if invalid
 *
 * Since: 1.0.8
 **/
gchar *
fu_common_get_path(FuPathKind path_kind)
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
		basedir = fu_common_get_win32_basedir();
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
		basedir = fu_common_get_win32_basedir();
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
		basedir = fu_common_get_win32_basedir();
		if (basedir != NULL)
			return g_build_filename(basedir, FWUPD_DATADIR, PACKAGE_NAME, NULL);
		return g_build_filename(FWUPD_DATADIR, PACKAGE_NAME, NULL);
	/* /usr/share/fwupd/quirks.d */
	case FU_PATH_KIND_DATADIR_QUIRKS:
		tmp = g_getenv("FWUPD_DATADIR_QUIRKS");
		if (tmp != NULL)
			return g_strdup(tmp);
		basedir = fu_common_get_path(FU_PATH_KIND_DATADIR_PKG);
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
		basedir = fu_common_get_path(FU_PATH_KIND_SYSCONFDIR);
		return g_build_filename(basedir, PACKAGE_NAME, NULL);
	/* /var/lib/fwupd */
	case FU_PATH_KIND_LOCALSTATEDIR_PKG:
		tmp = g_getenv("STATE_DIRECTORY");
		if (tmp != NULL && g_file_test(tmp, G_FILE_TEST_EXISTS))
			return g_build_filename(tmp, NULL);
		basedir = fu_common_get_path(FU_PATH_KIND_LOCALSTATEDIR);
		return g_build_filename(basedir, "lib", PACKAGE_NAME, NULL);
	/* /var/lib/fwupd/quirks.d */
	case FU_PATH_KIND_LOCALSTATEDIR_QUIRKS:
		tmp = g_getenv("FWUPD_LOCALSTATEDIR_QUIRKS");
		if (tmp != NULL)
			return g_build_filename(tmp, NULL);
		basedir = fu_common_get_path(FU_PATH_KIND_LOCALSTATEDIR_PKG);
		return g_build_filename(basedir, "quirks.d", NULL);
	/* /var/lib/fwupd/metadata */
	case FU_PATH_KIND_LOCALSTATEDIR_METADATA:
		tmp = g_getenv("FWUPD_LOCALSTATEDIR_METADATA");
		if (tmp != NULL)
			return g_build_filename(tmp, NULL);
		basedir = fu_common_get_path(FU_PATH_KIND_LOCALSTATEDIR_PKG);
		return g_build_filename(basedir, "metadata", NULL);
	/* /var/lib/fwupd/remotes.d */
	case FU_PATH_KIND_LOCALSTATEDIR_REMOTES:
		tmp = g_getenv("FWUPD_LOCALSTATEDIR_REMOTES");
		if (tmp != NULL)
			return g_build_filename(tmp, NULL);
		basedir = fu_common_get_path(FU_PATH_KIND_LOCALSTATEDIR_PKG);
		return g_build_filename(basedir, "remotes.d", NULL);
	/* /var/cache/fwupd */
	case FU_PATH_KIND_CACHEDIR_PKG:
		tmp = g_getenv("CACHE_DIRECTORY");
		if (tmp != NULL && g_file_test(tmp, G_FILE_TEST_EXISTS))
			return g_build_filename(tmp, NULL);
		basedir = fu_common_get_path(FU_PATH_KIND_LOCALSTATEDIR);
		return g_build_filename(basedir, "cache", PACKAGE_NAME, NULL);
	/* /var/etc/fwupd */
	case FU_PATH_KIND_LOCALCONFDIR_PKG:
		tmp = g_getenv("LOCALCONF_DIRECTORY");
		if (tmp != NULL && g_file_test(tmp, G_FILE_TEST_EXISTS))
			return g_build_filename(tmp, NULL);
		basedir = fu_common_get_path(FU_PATH_KIND_LOCALSTATEDIR);
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
		return fu_common_get_win32_basedir();
	/* this shouldn't happen */
	default:
		g_warning("cannot build path for unknown kind %u", path_kind);
	}

	return NULL;
}

/**
 * fu_common_fnmatch:
 * @pattern: a glob pattern, e.g. `*foo*`
 * @str: a string to match against the pattern, e.g. `bazfoobar`
 *
 * Matches a string against a glob pattern.
 *
 * Returns: %TRUE if the string matched
 *
 * Since: 1.3.5
 **/
gboolean
fu_common_fnmatch(const gchar *pattern, const gchar *str)
{
	g_return_val_if_fail(pattern != NULL, FALSE);
	g_return_val_if_fail(str != NULL, FALSE);
	return fu_common_fnmatch_impl(pattern, str);
}

static gint
fu_common_filename_glob_sort_cb(gconstpointer a, gconstpointer b)
{
	return g_strcmp0(*(const gchar **)a, *(const gchar **)b);
}

/**
 * fu_common_filename_glob:
 * @directory: a directory path
 * @pattern: a glob pattern, e.g. `*foo*`
 * @error: (nullable): optional return location for an error
 *
 * Returns all the filenames that match a specific glob pattern.
 * Any results are sorted. No matching files will set @error.
 *
 * Returns:  (element-type utf8) (transfer container): matching files, or %NULL
 *
 * Since: 1.5.0
 **/
GPtrArray *
fu_common_filename_glob(const gchar *directory, const gchar *pattern, GError **error)
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
		if (!fu_common_fnmatch(pattern, basename))
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
	g_ptr_array_sort(files, fu_common_filename_glob_sort_cb);
	return g_steal_pointer(&files);
}

/**
 * fu_common_cpuid:
 * @leaf: the CPUID level, now called the 'leaf' by Intel
 * @eax: (out) (nullable): EAX register
 * @ebx: (out) (nullable): EBX register
 * @ecx: (out) (nullable): ECX register
 * @edx: (out) (nullable): EDX register
 * @error: (nullable): optional return location for an error
 *
 * Calls CPUID and returns the registers for the given leaf.
 *
 * Returns: %TRUE if the registers are set.
 *
 * Since: 1.5.0
 **/
gboolean
fu_common_cpuid(guint32 leaf,
		guint32 *eax,
		guint32 *ebx,
		guint32 *ecx,
		guint32 *edx,
		GError **error)
{
#ifdef HAVE_CPUID_H
	guint eax_tmp = 0;
	guint ebx_tmp = 0;
	guint ecx_tmp = 0;
	guint edx_tmp = 0;

	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* get vendor */
	__get_cpuid_count(leaf, 0x0, &eax_tmp, &ebx_tmp, &ecx_tmp, &edx_tmp);
	if (eax != NULL)
		*eax = eax_tmp;
	if (ebx != NULL)
		*ebx = ebx_tmp;
	if (ecx != NULL)
		*ecx = ecx_tmp;
	if (edx != NULL)
		*edx = edx_tmp;
	return TRUE;
#else
	g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "no <cpuid.h> support");
	return FALSE;
#endif
}

/**
 * fu_common_get_cpu_vendor:
 *
 * Uses CPUID to discover the CPU vendor.
 *
 * Returns: a CPU vendor, e.g. %FU_CPU_VENDOR_AMD if the vendor was AMD.
 *
 * Since: 1.5.5
 **/
FuCpuVendor
fu_common_get_cpu_vendor(void)
{
#ifdef HAVE_CPUID_H
	guint ebx = 0;
	guint ecx = 0;
	guint edx = 0;

	if (fu_common_cpuid(0x0, NULL, &ebx, &ecx, &edx, NULL)) {
		if (ebx == signature_INTEL_ebx && edx == signature_INTEL_edx &&
		    ecx == signature_INTEL_ecx) {
			return FU_CPU_VENDOR_INTEL;
		}
		if (ebx == signature_AMD_ebx && edx == signature_AMD_edx &&
		    ecx == signature_AMD_ecx) {
			return FU_CPU_VENDOR_AMD;
		}
	}
#endif

	/* failed */
	return FU_CPU_VENDOR_UNKNOWN;
}

/**
 * fu_common_is_live_media:
 *
 * Checks if the user is running from a live media using various heuristics.
 *
 * Returns: %TRUE if live
 *
 * Since: 1.4.6
 **/
gboolean
fu_common_is_live_media(void)
{
	gsize bufsz = 0;
	g_autofree gchar *buf = NULL;
	g_auto(GStrv) tokens = NULL;
	const gchar *args[] = {
	    "rd.live.image",
	    "boot=live",
	    NULL, /* last entry */
	};
	if (g_file_test("/cdrom/.disk/info", G_FILE_TEST_EXISTS))
		return TRUE;
	if (!g_file_get_contents("/proc/cmdline", &buf, &bufsz, NULL))
		return FALSE;
	if (bufsz == 0)
		return FALSE;
	tokens = fu_strsplit(buf, bufsz - 1, " ", -1);
	for (guint i = 0; args[i] != NULL; i++) {
		if (g_strv_contains((const gchar *const *)tokens, args[i]))
			return TRUE;
	}
	return FALSE;
}

/**
 * fu_common_get_memory_size:
 *
 * Returns the size of physical memory.
 *
 * Returns: bytes
 *
 * Since: 1.5.6
 **/
guint64
fu_common_get_memory_size(void)
{
	return fu_common_get_memory_size_impl();
}

const gchar *
fu_common_convert_to_gpt_type(const gchar *type)
{
	struct {
		const gchar *gpt;
		const gchar *mbrs[4];
	} typeguids[] = {{"c12a7328-f81f-11d2-ba4b-00a0c93ec93b", /* esp */
			  {"0xef", "efi", NULL}},
			 {"ebd0a0a2-b9e5-4433-87c0-68b6b72699c7", /* fat32 */
			  {"0x0b", "fat32", "fat32lba", NULL}},
			 {NULL, {NULL}}};
	for (guint i = 0; typeguids[i].gpt != NULL; i++) {
		for (guint j = 0; typeguids[i].mbrs[j] != NULL; j++) {
			if (g_strcmp0(type, typeguids[i].mbrs[j]) == 0)
				return typeguids[i].gpt;
		}
	}
	return type;
}

/**
 * fu_common_check_full_disk_encryption:
 * @error: (nullable): optional return location for an error
 *
 * Checks that all FDE volumes are not going to be affected by a firmware update. If unsure,
 * return with failure and let the user decide.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.7.1
 **/
gboolean
fu_common_check_full_disk_encryption(GError **error)
{
	g_autoptr(GPtrArray) devices = NULL;

	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	devices = fu_common_get_block_devices(error);
	if (devices == NULL)
		return FALSE;
	for (guint i = 0; i < devices->len; i++) {
		GDBusProxy *proxy = g_ptr_array_index(devices, i);
		g_autoptr(GVariant) id_type = g_dbus_proxy_get_cached_property(proxy, "IdType");
		g_autoptr(GVariant) device = g_dbus_proxy_get_cached_property(proxy, "Device");
		if (id_type == NULL || device == NULL)
			continue;
		if (g_strcmp0(g_variant_get_string(id_type, NULL), "BitLocker") == 0) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_WOULD_BLOCK,
				    "%s device %s is encrypted",
				    g_variant_get_string(id_type, NULL),
				    g_variant_get_bytestring(device));
			return FALSE;
		}
	}
	return TRUE;
}

/**
 * fu_common_get_volumes_by_kind:
 * @kind: a volume kind, typically a GUID
 * @error: (nullable): optional return location for an error
 *
 * Finds all volumes of a specific partition type
 *
 * Returns: (transfer container) (element-type FuVolume): a #GPtrArray, or %NULL if the kind was not
 *found
 *
 * Since: 1.4.6
 **/
GPtrArray *
fu_common_get_volumes_by_kind(const gchar *kind, GError **error)
{
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(GPtrArray) volumes = NULL;

	g_return_val_if_fail(kind != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	devices = fu_common_get_block_devices(error);
	if (devices == NULL)
		return NULL;
	volumes = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	for (guint i = 0; i < devices->len; i++) {
		GDBusProxy *proxy_blk = g_ptr_array_index(devices, i);
		const gchar *type_str;
		g_autoptr(FuVolume) vol = NULL;
		g_autoptr(GDBusProxy) proxy_part = NULL;
		g_autoptr(GDBusProxy) proxy_fs = NULL;
		g_autoptr(GVariant) val = NULL;

		proxy_part = g_dbus_proxy_new_sync(g_dbus_proxy_get_connection(proxy_blk),
						   G_DBUS_PROXY_FLAGS_NONE,
						   NULL,
						   UDISKS_DBUS_SERVICE,
						   g_dbus_proxy_get_object_path(proxy_blk),
						   UDISKS_DBUS_INTERFACE_PARTITION,
						   NULL,
						   error);
		if (proxy_part == NULL) {
			g_prefix_error(error,
				       "failed to initialize d-bus proxy %s: ",
				       g_dbus_proxy_get_object_path(proxy_blk));
			return NULL;
		}
		val = g_dbus_proxy_get_cached_property(proxy_part, "Type");
		if (val == NULL)
			continue;

		g_variant_get(val, "&s", &type_str);
		proxy_fs = g_dbus_proxy_new_sync(g_dbus_proxy_get_connection(proxy_blk),
						 G_DBUS_PROXY_FLAGS_NONE,
						 NULL,
						 UDISKS_DBUS_SERVICE,
						 g_dbus_proxy_get_object_path(proxy_blk),
						 UDISKS_DBUS_INTERFACE_FILESYSTEM,
						 NULL,
						 error);
		if (proxy_fs == NULL) {
			g_prefix_error(error,
				       "failed to initialize d-bus proxy %s: ",
				       g_dbus_proxy_get_object_path(proxy_blk));
			return NULL;
		}
		vol = g_object_new(FU_TYPE_VOLUME,
				   "proxy-block",
				   proxy_blk,
				   "proxy-filesystem",
				   proxy_fs,
				   NULL);

		/* convert reported type to GPT type */
		type_str = fu_common_convert_to_gpt_type(type_str);
		if (g_getenv("FWUPD_VERBOSE") != NULL) {
			g_autofree gchar *id_type = fu_volume_get_id_type(vol);
			g_debug("device %s, type: %s, internal: %d, fs: %s",
				g_dbus_proxy_get_object_path(proxy_blk),
				type_str,
				fu_volume_is_internal(vol),
				id_type);
		}
		if (g_strcmp0(type_str, kind) != 0)
			continue;
		g_ptr_array_add(volumes, g_steal_pointer(&vol));
	}
	if (volumes->len == 0) {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "no volumes of type %s", kind);
		return NULL;
	}
	return g_steal_pointer(&volumes);
}

/**
 * fu_common_get_volume_by_device:
 * @device: a device string, typically starting with `/dev/`
 * @error: (nullable): optional return location for an error
 *
 * Finds the first volume from the specified device.
 *
 * Returns: (transfer full): a volume, or %NULL if the device was not found
 *
 * Since: 1.5.1
 **/
FuVolume *
fu_common_get_volume_by_device(const gchar *device, GError **error)
{
	g_autoptr(GPtrArray) devices = NULL;

	g_return_val_if_fail(device != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* find matching block device */
	devices = fu_common_get_block_devices(error);
	if (devices == NULL)
		return NULL;
	for (guint i = 0; i < devices->len; i++) {
		GDBusProxy *proxy_blk = g_ptr_array_index(devices, i);
		g_autoptr(GVariant) val = NULL;
		val = g_dbus_proxy_get_cached_property(proxy_blk, "Device");
		if (val == NULL)
			continue;
		if (g_strcmp0(g_variant_get_bytestring(val), device) == 0) {
			g_autoptr(GDBusProxy) proxy_fs = NULL;
			g_autoptr(GError) error_local = NULL;
			proxy_fs = g_dbus_proxy_new_sync(g_dbus_proxy_get_connection(proxy_blk),
							 G_DBUS_PROXY_FLAGS_NONE,
							 NULL,
							 UDISKS_DBUS_SERVICE,
							 g_dbus_proxy_get_object_path(proxy_blk),
							 UDISKS_DBUS_INTERFACE_FILESYSTEM,
							 NULL,
							 &error_local);
			if (proxy_fs == NULL)
				g_debug("ignoring: %s", error_local->message);
			return g_object_new(FU_TYPE_VOLUME,
					    "proxy-block",
					    proxy_blk,
					    "proxy-filesystem",
					    proxy_fs,
					    NULL);
		}
	}

	/* failed */
	g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "no volumes for device %s", device);
	return NULL;
}

/**
 * fu_common_get_volume_by_devnum:
 * @devnum: a device number
 * @error: (nullable): optional return location for an error
 *
 * Finds the first volume from the specified device.
 *
 * Returns: (transfer full): a volume, or %NULL if the device was not found
 *
 * Since: 1.5.1
 **/
FuVolume *
fu_common_get_volume_by_devnum(guint32 devnum, GError **error)
{
	g_autoptr(GPtrArray) devices = NULL;

	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* find matching block device */
	devices = fu_common_get_block_devices(error);
	if (devices == NULL)
		return NULL;
	for (guint i = 0; i < devices->len; i++) {
		GDBusProxy *proxy_blk = g_ptr_array_index(devices, i);
		g_autoptr(GVariant) val = NULL;
		val = g_dbus_proxy_get_cached_property(proxy_blk, "DeviceNumber");
		if (val == NULL)
			continue;
		if (devnum == g_variant_get_uint64(val)) {
			return g_object_new(FU_TYPE_VOLUME, "proxy-block", proxy_blk, NULL);
		}
	}

	/* failed */
	g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "no volumes for devnum %u", devnum);
	return NULL;
}

/**
 * fu_common_get_esp_default:
 * @error: (nullable): optional return location for an error
 *
 * Gets the platform default ESP
 *
 * Returns: (transfer full): a volume, or %NULL if the ESP was not found
 *
 * Since: 1.4.6
 **/
FuVolume *
fu_common_get_esp_default(GError **error)
{
	const gchar *path_tmp;
	gboolean has_internal = FALSE;
	g_autoptr(GPtrArray) volumes_fstab = g_ptr_array_new();
	g_autoptr(GPtrArray) volumes_mtab = g_ptr_array_new();
	g_autoptr(GPtrArray) volumes_vfat = g_ptr_array_new();
	g_autoptr(GPtrArray) volumes = NULL;
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* for the test suite use local directory for ESP */
	path_tmp = g_getenv("FWUPD_UEFI_ESP_PATH");
	if (path_tmp != NULL)
		return fu_volume_new_from_mount_path(path_tmp);

	volumes = fu_common_get_volumes_by_kind(FU_VOLUME_KIND_ESP, &error_local);
	if (volumes == NULL) {
		g_debug("%s, falling back to %s", error_local->message, FU_VOLUME_KIND_BDP);
		volumes = fu_common_get_volumes_by_kind(FU_VOLUME_KIND_BDP, error);
		if (volumes == NULL) {
			g_prefix_error(error, "%s: ", error_local->message);
			return NULL;
		}
	}

	/* are there _any_ internal vfat partitions?
	 * remember HintSystem is just that -- a hint! */
	for (guint i = 0; i < volumes->len; i++) {
		FuVolume *vol = g_ptr_array_index(volumes, i);
		g_autofree gchar *type = fu_volume_get_id_type(vol);
		if (g_strcmp0(type, "vfat") == 0 && fu_volume_is_internal(vol)) {
			has_internal = TRUE;
			break;
		}
	}

	/* filter to vfat partitions */
	for (guint i = 0; i < volumes->len; i++) {
		FuVolume *vol = g_ptr_array_index(volumes, i);
		g_autofree gchar *type = fu_volume_get_id_type(vol);
		if (type == NULL)
			continue;
		if (has_internal && !fu_volume_is_internal(vol))
			continue;
		if (g_strcmp0(type, "vfat") == 0)
			g_ptr_array_add(volumes_vfat, vol);
	}
	if (volumes_vfat->len == 0) {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_FILENAME, "No ESP found");
		return NULL;
	}
	for (guint i = 0; i < volumes_vfat->len; i++) {
		FuVolume *vol = g_ptr_array_index(volumes_vfat, i);
		g_ptr_array_add(fu_volume_is_mounted(vol) ? volumes_mtab : volumes_fstab, vol);
	}
	if (volumes_mtab->len == 1) {
		FuVolume *vol = g_ptr_array_index(volumes_mtab, 0);
		return g_object_ref(vol);
	}
	if (volumes_mtab->len == 0 && volumes_fstab->len == 1) {
		FuVolume *vol = g_ptr_array_index(volumes_fstab, 0);
		return g_object_ref(vol);
	}
	g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_FILENAME, "More than one available ESP");
	return NULL;
}

/**
 * fu_common_get_esp_for_path:
 * @esp_path: a path to the ESP
 * @error: (nullable): optional return location for an error
 *
 * Gets the platform ESP using a UNIX or UDisks path
 *
 * Returns: (transfer full): a #volume, or %NULL if the ESP was not found
 *
 * Since: 1.4.6
 **/
FuVolume *
fu_common_get_esp_for_path(const gchar *esp_path, GError **error)
{
	g_autofree gchar *basename = NULL;
	g_autoptr(GPtrArray) volumes = NULL;
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail(esp_path != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	volumes = fu_common_get_volumes_by_kind(FU_VOLUME_KIND_ESP, &error_local);
	if (volumes == NULL) {
		/* check if it's a valid directory already */
		if (g_file_test(esp_path, G_FILE_TEST_IS_DIR))
			return fu_volume_new_from_mount_path(esp_path);
		g_propagate_error(error, g_steal_pointer(&error_local));
		return NULL;
	}
	basename = g_path_get_basename(esp_path);
	for (guint i = 0; i < volumes->len; i++) {
		FuVolume *vol = g_ptr_array_index(volumes, i);
		g_autofree gchar *vol_basename =
		    g_path_get_basename(fu_volume_get_mount_point(vol));
		if (g_strcmp0(basename, vol_basename) == 0)
			return g_object_ref(vol);
	}
	g_set_error(error,
		    G_IO_ERROR,
		    G_IO_ERROR_INVALID_FILENAME,
		    "No ESP with path %s",
		    esp_path);
	return NULL;
}

/**
 * fu_common_align_up:
 * @value: value to align
 * @alignment: align to this power of 2, where 0x1F is the maximum value of 2GB
 *
 * Align a value to a power of 2 boundary, where @alignment is the bit position
 * to align to. If @alignment is zero then @value is always returned unchanged.
 *
 * Returns: aligned value, which will be the same as @value if already aligned,
 * 		or %G_MAXSIZE if the value would overflow
 *
 * Since: 1.6.0
 **/
gsize
fu_common_align_up(gsize value, guint8 alignment)
{
	gsize value_new;
	guint32 mask = 1 << alignment;

	g_return_val_if_fail(alignment <= FU_FIRMWARE_ALIGNMENT_2G, G_MAXSIZE);

	/* no alignment required */
	if ((value & (mask - 1)) == 0)
		return value;

	/* increment up to the next alignment value */
	value_new = value + mask;
	value_new &= ~(mask - 1);

	/* overflow */
	if (value_new < value)
		return G_MAXSIZE;

	/* success */
	return value_new;
}

/**
 * fu_battery_state_to_string:
 * @battery_state: a battery state, e.g. %FU_BATTERY_STATE_FULLY_CHARGED
 *
 * Converts an enumerated type to a string.
 *
 * Returns: a string, or %NULL for invalid
 *
 * Since: 1.6.0
 **/
const gchar *
fu_battery_state_to_string(FuBatteryState battery_state)
{
	if (battery_state == FU_BATTERY_STATE_UNKNOWN)
		return "unknown";
	if (battery_state == FU_BATTERY_STATE_CHARGING)
		return "charging";
	if (battery_state == FU_BATTERY_STATE_DISCHARGING)
		return "discharging";
	if (battery_state == FU_BATTERY_STATE_EMPTY)
		return "empty";
	if (battery_state == FU_BATTERY_STATE_FULLY_CHARGED)
		return "fully-charged";
	return NULL;
}

/**
 * fu_lid_state_to_string:
 * @lid_state: a battery state, e.g. %FU_LID_STATE_CLOSED
 *
 * Converts an enumerated type to a string.
 *
 * Returns: a string, or %NULL for invalid
 *
 * Since: 1.7.4
 **/
const gchar *
fu_lid_state_to_string(FuLidState lid_state)
{
	if (lid_state == FU_LID_STATE_UNKNOWN)
		return "unknown";
	if (lid_state == FU_LID_STATE_OPEN)
		return "open";
	if (lid_state == FU_LID_STATE_CLOSED)
		return "closed";
	return NULL;
}

/**
 * fu_xmlb_builder_insert_kv:
 * @bn: #XbBuilderNode
 * @key: string key
 * @value: string value
 *
 * Convenience function to add an XML node with a string value. If @value is %NULL
 * then no member is added.
 *
 * Since: 1.6.0
 **/
void
fu_xmlb_builder_insert_kv(XbBuilderNode *bn, const gchar *key, const gchar *value)
{
	if (value == NULL)
		return;
	xb_builder_node_insert_text(bn, key, value, NULL);
}

/**
 * fu_xmlb_builder_insert_kx:
 * @bn: #XbBuilderNode
 * @key: string key
 * @value: integer value
 *
 * Convenience function to add an XML node with an integer value. If @value is 0
 * then no member is added.
 *
 * Since: 1.6.0
 **/
void
fu_xmlb_builder_insert_kx(XbBuilderNode *bn, const gchar *key, guint64 value)
{
	g_autofree gchar *value_hex = NULL;
	if (value == 0)
		return;
	value_hex = g_strdup_printf("0x%x", (guint)value);
	xb_builder_node_insert_text(bn, key, value_hex, NULL);
}

/**
 * fu_xmlb_builder_insert_kb:
 * @bn: #XbBuilderNode
 * @key: string key
 * @value: boolean value
 *
 * Convenience function to add an XML node with a boolean value.
 *
 * Since: 1.6.0
 **/
void
fu_xmlb_builder_insert_kb(XbBuilderNode *bn, const gchar *key, gboolean value)
{
	xb_builder_node_insert_text(bn, key, value ? "true" : "false", NULL);
}
