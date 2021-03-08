/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2015 Peter Jones <pjones@redhat.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#ifdef __linux__
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <gio/gunixinputstream.h>
#include <gio/gunixoutputstream.h>
#endif

#include "fu-common.h"
#include "fu-efivar.h"

#include "fwupd-error.h"

static gchar *
fu_efivar_get_path (void)
{
	g_autofree gchar *sysfsfwdir = fu_common_get_path (FU_PATH_KIND_SYSFSDIR_FW);
	return g_build_filename (sysfsfwdir, "efi", "efivars", NULL);
}

static gchar *
fu_efivar_get_filename (const gchar *guid, const gchar *name)
{
	g_autofree gchar *efivardir = fu_efivar_get_path ();
	return g_strdup_printf ("%s/%s-%s", efivardir, name, guid);
}

/**
 * fu_efivar_supported:
 * @error: #GError
 *
 * Determines if the kernel supports EFI variables
 *
 * Returns: %TRUE on success
 *
 * Since: 1.4.0
 **/
gboolean
fu_efivar_supported (GError **error)
{
#ifdef __linux__
	g_autofree gchar *efivardir = fu_efivar_get_path ();
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	if (!g_file_test (efivardir, G_FILE_TEST_IS_DIR)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "kernel efivars support missing: %s",
			     efivardir);
		return FALSE;
	}
	return TRUE;
#else
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "efivarfs not currently supported on Windows");
	return FALSE;
#endif
}

#ifdef __linux__
static gboolean
fu_efivar_set_immutable_fd (int fd,
			     gboolean value,
			     gboolean *value_old,
			     GError **error)
{
	guint flags;
	gboolean is_immutable;
	int rc;

	/* get existing status */
	rc = ioctl (fd, FS_IOC_GETFLAGS, &flags);
	if (rc < 0) {
		/* check for tmpfs */
		if (errno == ENOTTY || errno == ENOSYS) {
			is_immutable = FALSE;
		} else {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "failed to get flags: %s",
				     strerror (errno));
			return FALSE;
		}
	} else {
		is_immutable = (flags & FS_IMMUTABLE_FL) > 0;
	}

	/* save the old value */
	if (value_old != NULL)
		*value_old = is_immutable;

	/* is this already correct */
	if (value) {
		if (is_immutable)
			return TRUE;
		flags |= FS_IMMUTABLE_FL;
	} else {
		if (!is_immutable)
			return TRUE;
		flags &= ~FS_IMMUTABLE_FL;
	}

	/* set the new status */
	rc = ioctl (fd, FS_IOC_SETFLAGS, &flags);
	if (rc < 0) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "failed to set flags: %s",
			     strerror (errno));
		return FALSE;
	}
	return TRUE;
}
#endif

static gboolean
fu_efivar_set_immutable (const gchar *fn,
			  gboolean value,
			  gboolean *value_old,
			  GError **error)
{
#ifdef __linux__
	gint fd;
	g_autoptr(GInputStream) istr = NULL;

	/* open file readonly */
	fd = open (fn, O_RDONLY);
	if (fd < 0) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_FILENAME,
			     "failed to open: %s",
			     strerror (errno));
		return FALSE;
	}
	istr = g_unix_input_stream_new (fd, TRUE);
	return fu_efivar_set_immutable_fd (fd, value, value_old, error);
#else
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "efivarfs not currently supported on Windows");
	return FALSE;
#endif
}

/**
 * fu_efivar_delete:
 * @guid: Globally unique identifier
 * @name: Variable name
 * @error: #GError
 *
 * Removes a variable from NVRAM
 *
 * Returns: %TRUE on success
 *
 * Since: 1.4.0
 **/
gboolean
fu_efivar_delete (const gchar *guid, const gchar *name, GError **error)
{
	g_autofree gchar *fn = NULL;
	g_autoptr(GFile) file = NULL;

	g_return_val_if_fail (guid != NULL, FALSE);
	g_return_val_if_fail (name != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	fn = fu_efivar_get_filename (guid, name);
	file = g_file_new_for_path (fn);
	if (!g_file_query_exists (file, NULL))
		return TRUE;
	if (!fu_efivar_set_immutable (fn, FALSE, NULL, error)) {
		g_prefix_error (error, "failed to set %s as mutable: ", fn);
		return FALSE;
	}
	return g_file_delete (file, NULL, error);
}

/**
 * fu_efivar_delete_with_glob:
 * @guid: Globally unique identifier
 * @name_glob: Variable name
 * @error: #GError
 *
 * Removes a group of variables from NVRAM
 *
 * Returns: %TRUE on success
 *
 * Since: 1.4.0
 **/
gboolean
fu_efivar_delete_with_glob (const gchar *guid, const gchar *name_glob, GError **error)
{
	const gchar *fn;
	g_autofree gchar *nameguid_glob = NULL;
	g_autofree gchar *efivardir = fu_efivar_get_path ();
	g_autoptr(GDir) dir = NULL;

	g_return_val_if_fail (guid != NULL, FALSE);
	g_return_val_if_fail (name_glob != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	dir = g_dir_open (efivardir, 0, error);
	if (dir == NULL)
		return FALSE;
	nameguid_glob = g_strdup_printf ("%s-%s", name_glob, guid);
	while ((fn = g_dir_read_name (dir)) != NULL) {
		if (fu_common_fnmatch (nameguid_glob, fn)) {
			g_autofree gchar *keyfn = g_build_filename (efivardir, fn, NULL);
			g_autoptr(GFile) file = g_file_new_for_path (keyfn);
			if (!fu_efivar_set_immutable (keyfn, FALSE, NULL, error)) {
				g_prefix_error (error, "failed to set %s as mutable: ", keyfn);
				return FALSE;
			}
			if (!g_file_delete (file, NULL, error))
				return FALSE;
		}
	}
	return TRUE;
}

static gboolean
fu_efivar_exists_guid (const gchar *guid)
{
	const gchar *fn;
	g_autofree gchar *efivardir = fu_efivar_get_path ();
	g_autoptr(GDir) dir = NULL;

	dir = g_dir_open (efivardir, 0, NULL);
	if (dir == NULL)
		return FALSE;
	while ((fn = g_dir_read_name (dir)) != NULL) {
		if (g_str_has_suffix (fn, guid))
			return TRUE;
	}
	return TRUE;
}

/**
 * fu_efivar_exists:
 * @guid: Globally unique identifier
 * @name: (nullable): Variable name
 *
 * Test if a variable exists
 *
 * Returns: %TRUE on success
 *
 * Since: 1.4.0
 **/
gboolean
fu_efivar_exists (const gchar *guid, const gchar *name)
{
	g_autofree gchar *fn = NULL;

	g_return_val_if_fail (guid != NULL, FALSE);

	/* any name */
	if (name == NULL)
		return fu_efivar_exists_guid (guid);

	fn = fu_efivar_get_filename (guid, name);
	return g_file_test (fn, G_FILE_TEST_EXISTS);
}

/**
 * fu_efivar_get_data:
 * @guid: Globally unique identifier
 * @name: Variable name
 * @data: Data to set
 * @data_sz: Size of data
 * @attr: Attributes
 * @error: A #GError
 *
 * Gets the data from a UEFI variable in NVRAM
 *
 * Returns: %TRUE on success
 *
 * Since: 1.4.0
 **/
gboolean
fu_efivar_get_data (const gchar *guid, const gchar *name, guint8 **data,
		       gsize *data_sz, guint32 *attr, GError **error)
{
#ifdef __linux__
	gssize attr_sz;
	gssize data_sz_tmp;
	guint32 attr_tmp;
	guint64 sz;
	g_autofree gchar *fn = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GFileInfo) info = NULL;
	g_autoptr(GInputStream) istr = NULL;

	g_return_val_if_fail (guid != NULL, FALSE);
	g_return_val_if_fail (name != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* open file as stream */
	fn = fu_efivar_get_filename (guid, name);
	file = g_file_new_for_path (fn);
	istr = G_INPUT_STREAM (g_file_read (file, NULL, error));
	if (istr == NULL)
		return FALSE;
	info = g_file_input_stream_query_info (G_FILE_INPUT_STREAM (istr),
					       G_FILE_ATTRIBUTE_STANDARD_SIZE,
					       NULL, error);
	if (info == NULL) {
		g_prefix_error (error, "failed to get stream info: ");
		return FALSE;
	}

	/* get total stream size */
	sz = g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_STANDARD_SIZE);
	if (sz < 4) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "efivars file too small: %" G_GUINT64_FORMAT, sz);
		return FALSE;
	}

	/* read out the attributes */
	attr_sz = g_input_stream_read (istr, &attr_tmp, sizeof(attr_tmp), NULL, error);
	if (attr_sz == -1) {
		g_prefix_error (error, "failed to read attr: ");
		return FALSE;
	}
	if (attr != NULL)
		*attr = attr_tmp;

	/* read out the data */
	data_sz_tmp = sz - sizeof(attr_tmp);
	if (data_sz != NULL)
		*data_sz = data_sz_tmp;
	if (data != NULL) {
		g_autofree guint8 *data_tmp = g_malloc0 (data_sz_tmp);
		if (!g_input_stream_read_all (istr, data_tmp, data_sz_tmp,
					      NULL, NULL, error)) {
			g_prefix_error (error, "failed to read data: ");
			return FALSE;
		}
		*data = g_steal_pointer (&data_tmp);
	}
	return TRUE;
#else
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "efivarfs not currently supported on Windows");
	return FALSE;
#endif
}

/**
 * fu_efivar_get_data_bytes:
 * @guid: Globally unique identifier
 * @name: Variable name
 * @attr: (nullable): Attributes
 * @error: A #GError
 *
 * Gets the data from a UEFI variable in NVRAM
 *
 * Returns: (transfer full): a #GBytes, or %NULL
 *
 * Since: 1.5.0
 **/
GBytes *
fu_efivar_get_data_bytes (const gchar *guid,
			  const gchar *name,
			  guint32 *attr,
			  GError **error)
{
	guint8 *data = NULL;
	gsize datasz = 0;

	g_return_val_if_fail (guid != NULL, NULL);
	g_return_val_if_fail (name != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	if (!fu_efivar_get_data (guid, name, &data, &datasz, attr, error))
		return NULL;
	return g_bytes_new_take (data, datasz);
}

/**
 * fu_efivar_get_names:
 * @guid: Globally unique identifier
 * @error: A #GError
 *
 * Gets the list of names where the GUID matches. An error is set if there are
 * no names matching the GUID.
 *
 * Returns: (transfer container) (element-type utf8): array of names
 *
 * Since: 1.4.7
 **/
GPtrArray *
fu_efivar_get_names (const gchar *guid, GError **error)
{
	const gchar *name_guid;
	g_autofree gchar *path = fu_efivar_get_path ();
	g_autoptr(GDir) dir = NULL;
	g_autoptr(GPtrArray) names = g_ptr_array_new_with_free_func (g_free);

	g_return_val_if_fail (guid != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* find names with matching GUID */
	dir = g_dir_open (path, 0, error);
	if (dir == NULL)
		return NULL;
	while ((name_guid = g_dir_read_name (dir)) != NULL) {
		gsize name_guidsz = strlen (name_guid);
		if (name_guidsz < 38)
			continue;
		if (g_strcmp0 (name_guid + name_guidsz - 36, guid) == 0) {
			g_ptr_array_add (names,
					 g_strndup (name_guid, name_guidsz - 37));
		}
	}

	/* nothing found */
	if (names->len == 0) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_FOUND,
			     "no names for GUID %s", guid);
		return NULL;
	}

	/* success */
	return g_steal_pointer (&names);
}

/**
 * fu_efivar_get_monitor:
 * @guid: Globally unique identifier
 * @name: Variable name
 * @error: A #GError
 *
 * Returns a file monitor for a specific key.
 *
 * Returns: (transfer full): a #GFileMonitor, or %NULL for an error
 *
 * Since: 1.5.5
 **/
GFileMonitor *
fu_efivar_get_monitor (const gchar *guid, const gchar *name, GError **error)
{
	g_autofree gchar *fn = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GFileMonitor) monitor = NULL;

	g_return_val_if_fail (guid != NULL, NULL);
	g_return_val_if_fail (name != NULL, NULL);

	fn = fu_efivar_get_filename (guid, name);
	file = g_file_new_for_path (fn);
	monitor = g_file_monitor_file (file, G_FILE_MONITOR_NONE, NULL, error);
	if (monitor == NULL)
		return NULL;
	g_file_monitor_set_rate_limit (monitor, 5000);
	return g_steal_pointer (&monitor);
}

/**
 * fu_efivar_space_used:
 * @error: A #GError
 *
 * Gets the total size used by all EFI variables. This may be less than the size reported by the
 * kernel as some (hopefully small) variables are hidden from userspace.
 *
 * Returns: total allocated size of all visible variables, or %G_MAXUINT64 on error
 *
 * Since: 1.5.1
 **/
guint64
fu_efivar_space_used (GError **error)
{
	const gchar *fn;
	guint64 total = 0;
	g_autoptr(GDir) dir = NULL;
	g_autofree gchar *path = fu_efivar_get_path ();

	g_return_val_if_fail (error == NULL || *error == NULL, G_MAXUINT64);

	/* stat each file */
	dir = g_dir_open (path, 0, error);
	if (dir == NULL)
		return G_MAXUINT64;
	while ((fn = g_dir_read_name (dir)) != NULL) {
		guint64 sz;
		g_autofree gchar *pathfn = g_build_filename (path, fn, NULL);
		g_autoptr(GFile) file = g_file_new_for_path (pathfn);
		g_autoptr(GFileInfo) info = NULL;

		info = g_file_query_info (file,
					  G_FILE_ATTRIBUTE_STANDARD_ALLOCATED_SIZE ","
					  G_FILE_ATTRIBUTE_STANDARD_SIZE,
					  G_FILE_QUERY_INFO_NONE,
					  NULL, error);
		if (info == NULL)
			return G_MAXUINT64;
		sz = g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_STANDARD_ALLOCATED_SIZE);
		if (sz == 0)
			sz = g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_STANDARD_SIZE);
		total += sz;
	}

	/* success */
	return total;
}
/**
 * fu_efivar_set_data:
 * @guid: Globally unique identifier
 * @name: Variable name
 * @data: Data to set
 * @sz: Size of data
 * @attr: Attributes
 * @error: A #GError
 *
 * Sets the data to a UEFI variable in NVRAM
 *
 * Returns: %TRUE on success
 *
 * Since: 1.4.0
 **/
gboolean
fu_efivar_set_data (const gchar *guid, const gchar *name, const guint8 *data,
		     gsize sz, guint32 attr, GError **error)
{
#ifdef __linux__
	int fd;
	int open_wflags;
	gboolean was_immutable;
	g_autofree gchar *fn = fu_efivar_get_filename (guid, name);
	g_autofree guint8 *buf = g_malloc0 (sizeof(guint32) + sz);
	g_autoptr(GFile) file = g_file_new_for_path (fn);
	g_autoptr(GOutputStream) ostr = NULL;

	g_return_val_if_fail (guid != NULL, FALSE);
	g_return_val_if_fail (name != NULL, FALSE);
	g_return_val_if_fail (data != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* create empty file so we can clear the immutable bit before writing */
	if (!g_file_query_exists (file, NULL)) {
		g_autoptr(GFileOutputStream) ostr_tmp = NULL;
		ostr_tmp = g_file_create (file,
					  G_FILE_CREATE_NONE,
					  NULL,
					  error);
		if (ostr_tmp == NULL)
			return FALSE;
		if (!g_output_stream_close (G_OUTPUT_STREAM (ostr_tmp), NULL, error)) {
			g_prefix_error (error, "failed to touch efivarfs: ");
			return FALSE;
		}
	}
	if (!fu_efivar_set_immutable (fn, FALSE, &was_immutable, error)) {
		g_prefix_error (error, "failed to set %s as mutable: ", fn);
		return FALSE;
	}

	/* open file for writing, optionally append */
	open_wflags = O_WRONLY;
	if (attr & FU_EFIVAR_ATTR_APPEND_WRITE)
		open_wflags |= O_APPEND;
	fd = open (fn, open_wflags);
	if (fd < 0) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "failed to open %s: %s",
			     fn, strerror (errno));
		return FALSE;
	}
	ostr = g_unix_output_stream_new (fd, TRUE);
	memcpy (buf, &attr, sizeof(attr));
	memcpy (buf + sizeof(attr), data, sz);
	if (g_output_stream_write (ostr, buf, sizeof(attr) + sz, NULL, error) < 0) {
		g_prefix_error (error, "failed to write data to efivarfs: ");
		return FALSE;
	}

	/* set as immutable again */
	if (was_immutable && !fu_efivar_set_immutable (fn, TRUE, NULL, error)) {
		g_prefix_error (error, "failed to set %s as immutable: ", fn);
		return FALSE;
	}

	/* success */
	return TRUE;
#else
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "efivarfs not currently supported on Windows");
	return FALSE;
#endif
}

/**
 * fu_efivar_set_data_bytes:
 * @guid: Globally unique identifier
 * @name: Variable name
 * @bytes: a #GBytes
 * @attr: Attributes
 * @error: A #GError
 *
 * Sets the data to a UEFI variable in NVRAM
 *
 * Returns: %TRUE on success
 *
 * Since: 1.5.0
 **/
gboolean
fu_efivar_set_data_bytes (const gchar *guid, const gchar *name, GBytes *bytes,
			  guint32 attr, GError **error)
{
	gsize bufsz = 0;
	const guint8 *buf;

	g_return_val_if_fail (guid != NULL, FALSE);
	g_return_val_if_fail (name != NULL, FALSE);
	g_return_val_if_fail (bytes != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	buf = g_bytes_get_data (bytes, &bufsz);
	return fu_efivar_set_data (guid, name, buf, bufsz, attr, error);
}

/**
 * fu_efivar_secure_boot_enabled_full:
 * @error: A #GError
 *
 * Determines if secure boot was enabled
 *
 * Returns: %TRUE on success
 *
 * Since: 1.5.0
 **/
gboolean
fu_efivar_secure_boot_enabled_full (GError **error)
{
	gsize data_size = 0;
	g_autofree guint8 *data = NULL;

	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (!fu_efivar_get_data (FU_EFIVAR_GUID_EFI_GLOBAL, "SecureBoot",
				 &data, &data_size, NULL, NULL)) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "SecureBoot is not available");
		return FALSE;
	}
	if (data_size >= 1 && data[0] & 1)
		return TRUE;

	/* available, but not enabled */
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_FOUND,
			     "SecureBoot is not enabled");
	return FALSE;
}

/**
 * fu_efivar_secure_boot_enabled:
 *
 * Determines if secure boot was enabled
 *
 * Returns: %TRUE on success
 *
 * Since: 1.4.0
 **/
gboolean
fu_efivar_secure_boot_enabled (void)
{
	return fu_efivar_secure_boot_enabled_full (NULL);
}
