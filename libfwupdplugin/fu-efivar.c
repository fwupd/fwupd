/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2015-2017 Peter Jones <pjones@redhat.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#ifndef _WIN32
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
#ifndef _WIN32
	g_autofree gchar *efivardir = fu_efivar_get_path ();
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
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "efivarfs not currently supported on Windows");
	return FALSE;
#endif
}

static gboolean
fu_efivar_set_immutable_fd (int fd,
			     gboolean value,
			     gboolean *value_old,
			     GError **error)
{
#ifndef _WIN32
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
#else
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "efivarfs not currently supported on Windows");
	return FALSE;
#endif
}

static gboolean
fu_efivar_set_immutable (const gchar *fn,
			  gboolean value,
			  gboolean *value_old,
			  GError **error)
{
#ifndef _WIN32
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
	g_autofree gchar *fn = fu_efivar_get_filename (guid, name);
	g_autoptr(GFile) file = g_file_new_for_path (fn);
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
	g_autoptr(GDir) dir = g_dir_open (efivardir, 0, error);
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

/**
 * fu_efivar_exists:
 * @guid: Globally unique identifier
 * @name: Variable name
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
	g_autofree gchar *fn = fu_efivar_get_filename (guid, name);
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
#ifndef _WIN32
	gssize attr_sz;
	gssize data_sz_tmp;
	guint32 attr_tmp;
	guint64 sz;
	g_autofree gchar *fn = fu_efivar_get_filename (guid, name);
	g_autoptr(GFile) file = g_file_new_for_path (fn);
	g_autoptr(GFileInfo) info = NULL;
	g_autoptr(GInputStream) istr = NULL;

	/* open file as stream */
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
#ifndef _WIN32
	int fd;
	gboolean was_immutable;
	g_autofree gchar *fn = fu_efivar_get_filename (guid, name);
	g_autofree guint8 *buf = g_malloc0 (sizeof(guint32) + sz);
	g_autoptr(GFile) file = g_file_new_for_path (fn);
	g_autoptr(GOutputStream) ostr = NULL;

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

	/* open file for writing */
	fd = open (fn, O_WRONLY);
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
 * fu_efivar_secure_boot_enabled:
 * @error: #GError
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
	gsize data_size = 0;
	g_autofree guint8 *data = NULL;

	if (!fu_efivar_get_data (FU_EFIVAR_GUID_EFI_GLOBAL, "SecureBoot",
				    &data, &data_size, NULL, NULL))
		return FALSE;
	if (data_size >= 1 && data[0] & 1)
		return TRUE;
	return FALSE;
}
