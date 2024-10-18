/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuCommon"

#include "config.h"

#include <errno.h>
#include <glib/gstdio.h>
#ifdef HAVE_UTSNAME_H
#include <sys/utsname.h>
#endif

#include "fu-common.h"
#include "fu-input-stream.h"
#include "fu-kernel.h"
#include "fu-path.h"
#include "fu-string.h"
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

	/* sanity check */
	if (contents == NULL || sz == 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "failed to get firmware search path from %s",
			    sys_fw_search_path);
		return NULL;
	}

	/* remove newline character */
	if (contents[sz - 1] == '\n')
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
	g_autofree gchar *sys_fw_search_path_prm = NULL;

	g_return_val_if_fail(path != NULL, FALSE);
	g_return_val_if_fail(strlen(path) < PATH_MAX, FALSE);

	g_debug("writing firmware search path (%" G_GSIZE_FORMAT "): %s", strlen(path), path);
	sys_fw_search_path_prm = fu_path_from_kind(FU_PATH_KIND_FIRMWARE_SEARCH);
	return g_file_set_contents_full(sys_fw_search_path_prm,
					path,
					strlen(path),
					G_FILE_SET_CONTENTS_NONE,
					0644,
					error);
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

typedef struct {
	GHashTable *hash;
	GHashTable *values;
} FuKernelConfigHelper;

static gboolean
fu_kernel_parse_config_line_cb(GString *token, guint token_idx, gpointer user_data, GError **error)
{
	g_auto(GStrv) kv = NULL;
	FuKernelConfigHelper *helper = (FuKernelConfigHelper *)user_data;
	GRefString *value;

	if (token->len == 0)
		return TRUE;
	if (token->str[0] == '#')
		return TRUE;

	kv = g_strsplit(token->str, "=", 2);
	if (g_strv_length(kv) != 2) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "invalid format for '%s'",
			    token->str);
		return FALSE;
	}
	value = g_hash_table_lookup(helper->values, kv[1]);
	if (value != NULL) {
		g_hash_table_insert(helper->hash, g_strdup(kv[0]), g_ref_string_acquire(value));
	} else {
		g_hash_table_insert(helper->hash, g_strdup(kv[0]), g_ref_string_new(kv[1]));
	}
	return TRUE;
}

/**
 * fu_kernel_parse_config:
 * @buf: (not nullable): cmdline to parse
 * @bufsz: size of @bufsz
 *
 * Parses all the kernel options into a hash table. Commented out options are not included.
 *
 * Returns: (transfer container) (element-type utf8 utf8): config keys
 *
 * Since: 1.9.6
 **/
GHashTable *
fu_kernel_parse_config(const gchar *buf, gsize bufsz, GError **error)
{
	g_autoptr(GHashTable) hash = g_hash_table_new_full(g_str_hash,
							   g_str_equal,
							   g_free,
							   (GDestroyNotify)g_ref_string_release);
	g_autoptr(GHashTable) values = g_hash_table_new_full(g_str_hash,
							     g_str_equal,
							     NULL,
							     (GDestroyNotify)g_ref_string_release);
	FuKernelConfigHelper helper = {.hash = hash, .values = values};
	const gchar *value_keys[] = {"y", "m", "0", NULL};

	g_return_val_if_fail(buf != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* add 99.9% of the most common keys to avoid thousands of small allocations */
	for (guint i = 0; value_keys[i] != NULL; i++) {
		g_hash_table_insert(values,
				    (gpointer)value_keys[i],
				    g_ref_string_new(value_keys[i]));
	}
	if (!fu_strsplit_full(buf, bufsz, "\n", fu_kernel_parse_config_line_cb, &helper, error))
		return NULL;
	return g_steal_pointer(&hash);
}

#ifdef __linux__
static gchar *
fu_kernel_get_config_path(GError **error)
{
#ifdef HAVE_UTSNAME_H
	struct utsname name_tmp;
	g_autofree gchar *config_fn = NULL;
	g_autofree gchar *bootdir = fu_path_from_kind(FU_PATH_KIND_HOSTFS_BOOT);

	memset(&name_tmp, 0, sizeof(struct utsname));
	if (uname(&name_tmp) < 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "failed to read kernel version");
		return NULL;
	}
	config_fn = g_strdup_printf("config-%s", name_tmp.release);
	return g_build_filename(bootdir, config_fn, NULL);
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "platform does not support uname");
	return NULL;
#endif
}
#endif

/**
 * fu_kernel_get_config:
 * @error: (nullable): optional return location for an error
 *
 * Loads all the kernel options into a hash table. Commented out options are not included.
 *
 * Returns: (transfer container) (element-type utf8 utf8): options from the kernel
 *
 * Since: 1.8.5
 **/
GHashTable *
fu_kernel_get_config(GError **error)
{
#ifdef __linux__
	gsize bufsz = 0;
	g_autofree gchar *buf = NULL;
	g_autofree gchar *fn = NULL;
	g_autofree gchar *procdir = fu_path_from_kind(FU_PATH_KIND_PROCFS);
	g_autofree gchar *config_fngz = g_build_filename(procdir, "config.gz", NULL);

	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* try /proc/config.gz -- which will only work with CONFIG_IKCONFIG */
	if (g_file_test(config_fngz, G_FILE_TEST_EXISTS)) {
		g_autoptr(GBytes) payload = NULL;
		g_autoptr(GConverter) conv = NULL;
		g_autoptr(GFile) file = g_file_new_for_path(config_fngz);
		g_autoptr(GInputStream) istream1 = NULL;
		g_autoptr(GInputStream) istream2 = NULL;

		istream1 = G_INPUT_STREAM(g_file_read(file, NULL, error));
		if (istream1 == NULL)
			return NULL;
		conv = G_CONVERTER(g_zlib_decompressor_new(G_ZLIB_COMPRESSOR_FORMAT_GZIP));
		istream2 = g_converter_input_stream_new(istream1, conv);
		payload = fu_input_stream_read_bytes(istream2, 0, G_MAXSIZE, NULL, error);
		if (payload == NULL)
			return NULL;
		return fu_kernel_parse_config(g_bytes_get_data(payload, NULL),
					      g_bytes_get_size(payload),
					      error);
	}

	/* fall back to /boot/config-$(uname -r) */
	fn = fu_kernel_get_config_path(error);
	if (fn == NULL)
		return NULL;
	if (!g_file_get_contents(fn, &buf, &bufsz, error))
		return NULL;
	return fu_kernel_parse_config(buf, bufsz, error);
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "platform does not support getting the kernel config");
	return NULL;
#endif
}

/**
 * fu_kernel_parse_cmdline:
 * @buf: (not nullable): cmdline to parse
 * @bufsz: size of @bufsz
 *
 * Parses all the kernel key/values into a hash table, respecting double quotes when required.
 *
 * Returns: (transfer container) (element-type utf8 utf8): keys from the cmdline
 *
 * Since: 1.9.1
 **/
GHashTable *
fu_kernel_parse_cmdline(const gchar *buf, gsize bufsz)
{
	gboolean is_escape = FALSE;
	g_autoptr(GHashTable) hash = NULL;
	g_autoptr(GString) acc = g_string_new(NULL);

	g_return_val_if_fail(buf != NULL, NULL);

	hash = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	if (bufsz == 0)
		return g_steal_pointer(&hash);
	for (gsize i = 0; i < bufsz; i++) {
		if (!is_escape && (buf[i] == ' ' || buf[i] == '\n') && acc->len > 0) {
			g_auto(GStrv) kv = g_strsplit(acc->str, "=", 2);
			g_hash_table_insert(hash, g_strdup(kv[0]), g_strdup(kv[1]));
			g_string_set_size(acc, 0);
			continue;
		}
		if (buf[i] == '"') {
			is_escape = !is_escape;
			continue;
		}
		g_string_append_c(acc, buf[i]);
	}
	if (acc->len > 0) {
		g_auto(GStrv) kv = g_strsplit(acc->str, "=", 2);
		g_hash_table_insert(hash, g_strdup(kv[0]), g_strdup(kv[1]));
	}

	/* success */
	return g_steal_pointer(&hash);
}

/**
 * fu_kernel_get_cmdline:
 * @error: (nullable): optional return location for an error
 *
 * Loads all the kernel /proc/cmdline key/values into a hash table.
 *
 * Returns: (transfer container) (element-type utf8 utf8): keys from the kernel command line
 *
 * Since: 1.8.5
 **/
GHashTable *
fu_kernel_get_cmdline(GError **error)
{
#ifdef __linux__
	gsize bufsz = 0;
	g_autofree gchar *buf = NULL;

	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	if (!g_file_get_contents("/proc/cmdline", &buf, &bufsz, error))
		return NULL;
	return fu_kernel_parse_cmdline(buf, bufsz);
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "platform does not support getting the kernel cmdline");
	return NULL;
#endif
}

gboolean
fu_kernel_check_cmdline_mutable(GError **error)
{
	g_autofree gchar *bootdir = fu_path_from_kind(FU_PATH_KIND_HOSTFS_BOOT);
	g_autofree gchar *grubby_path = NULL;
	g_autofree gchar *sysconfdir = fu_path_from_kind(FU_PATH_KIND_SYSCONFDIR);
	g_auto(GStrv) config_files = g_new0(gchar *, 3);

	/* not found */
	grubby_path = fu_path_find_program("grubby", error);
	if (grubby_path == NULL)
		return FALSE;

	/* check all the config files are writable */
	config_files[0] = g_build_filename(bootdir, "grub2", "grub.cfg", NULL);
	config_files[1] = g_build_filename(sysconfdir, "grub.cfg", NULL);
	for (guint i = 0; config_files[i] != NULL; i++) {
		g_autoptr(GFile) file = g_file_new_for_path(config_files[i]);
		g_autoptr(GFileInfo) info = NULL;
		g_autoptr(GError) error_local = NULL;

		if (!g_file_query_exists(file, NULL))
			continue;
		info = g_file_query_info(file,
					 G_FILE_ATTRIBUTE_ACCESS_CAN_WRITE,
					 G_FILE_QUERY_INFO_NONE,
					 NULL,
					 &error_local);
		if (info == NULL) {
			g_warning("failed to get info for %s: %s",
				  config_files[i],
				  error_local->message);
			continue;
		}
		if (!g_file_info_get_attribute_boolean(info, G_FILE_ATTRIBUTE_ACCESS_CAN_WRITE)) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "%s is not writable",
				    config_files[i]);
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_kernel_set_commandline(const gchar *arg, gboolean enable, GError **error)
{
	g_autofree gchar *output = NULL;
	g_autofree gchar *arg_string = NULL;
	g_autofree gchar *grubby_path = NULL;
	const gchar *argv_grubby[] = {"", "--update-kernel=DEFAULT", "", NULL};

	grubby_path = fu_path_find_program("grubby", error);
	if (grubby_path == NULL) {
		g_prefix_error(error, "failed to find grubby: ");
		return FALSE;
	}
	if (enable)
		arg_string = g_strdup_printf("--args=%s", arg);
	else
		arg_string = g_strdup_printf("--remove-args=%s", arg);

	argv_grubby[0] = grubby_path;
	argv_grubby[2] = arg_string;
	return g_spawn_sync(NULL,
			    (gchar **)argv_grubby,
			    NULL,
			    G_SPAWN_DEFAULT,
			    NULL,
			    NULL,
			    &output,
			    NULL,
			    NULL,
			    error);
}

/**
 * fu_kernel_add_cmdline_arg:
 * @arg: (not nullable): key to set
 * @error: (nullable): optional return location for an error
 *
 * Add a kernel command line argument.
 *
 * Returns: %TRUE if successful
 *
 * Since: 1.9.5
 **/
gboolean
fu_kernel_add_cmdline_arg(const gchar *arg, GError **error)
{
	return fu_kernel_set_commandline(arg, TRUE, error);
}

/**
 * fu_kernel_remove_cmdline_arg:
 * @arg: (not nullable): key to set
 * @error: (nullable): optional return location for an error
 *
 * Remove a kernel command line argument.
 *
 * Returns: %TRUE if successful
 *
 * Since: 1.9.5
 **/
gboolean
fu_kernel_remove_cmdline_arg(const gchar *arg, GError **error)
{
	return fu_kernel_set_commandline(arg, FALSE, error);
}
