/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2015-2017 Peter Jones <pjones@redhat.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <efivar.h>
#include <gio/gio.h>

#include "fu-common.h"
#include "fu-uefi-common.h"

#include "fwupd-common.h"

static const gchar *
fu_uefi_bootmgr_get_suffix (GError **error)
{
	guint64 firmware_bits;
	struct {
		guint64 bits;
		const gchar *arch;
	} suffixes[] = {
#if defined(__x86_64__)
		{ 64, "x64" },
#elif defined(__aarch64__)
		{ 64, "aa64" },
#endif
#if defined(__x86_64__) || defined(__i386__) || defined(__i686__)
		{ 32, "ia32" },
#endif
		{ 0, NULL }
	};
	g_autofree gchar *sysfsfwdir = fu_common_get_path (FU_PATH_KIND_SYSFSDIR_FW);
	g_autofree gchar *sysfsefidir = g_build_filename (sysfsfwdir, "efi", NULL);
	firmware_bits = fu_uefi_read_file_as_uint64 (sysfsefidir, "fw_platform_size");
	if (firmware_bits == 0) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_FOUND,
			     "%s/fw_platform_size cannot be found",
			     sysfsefidir);
		return NULL;
	}
	for (guint i = 0; suffixes[i].arch != NULL; i++) {
		if (firmware_bits != suffixes[i].bits)
			continue;
		return suffixes[i].arch;
	}

	/* this should exist */
	g_set_error (error,
		     G_IO_ERROR,
		     G_IO_ERROR_NOT_FOUND,
		     "%s/fw_platform_size has unknown value %" G_GUINT64_FORMAT,
		     sysfsefidir, firmware_bits);
	return NULL;
}

gchar *
fu_uefi_get_esp_app_path (const gchar *esp_mountpoint, const gchar *cmd, GError **error)
{
	const gchar *suffix = fu_uefi_bootmgr_get_suffix (error);
	g_autofree gchar *base = NULL;
	if (suffix == NULL)
		return NULL;
	base = fu_uefi_get_full_esp_path (esp_mountpoint);
	return g_strdup_printf ("%s/%s%s.efi", base, cmd, suffix);
}

gchar *
fu_uefi_get_built_app_path (GError **error)
{
	const gchar *extension = "";
	const gchar *suffix;
	g_autofree gchar *source_path = NULL;
	if (fu_uefi_secure_boot_enabled ())
		extension = ".signed";
	suffix = fu_uefi_bootmgr_get_suffix (error);
	if (suffix == NULL)
		return NULL;
	source_path = g_strdup_printf ("%s/fwup%s.efi%s",
				       EFI_APP_LOCATION,
				       suffix,
				       extension);
	if (!g_file_test (source_path, G_FILE_TEST_EXISTS)) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_FOUND,
			     "%s cannot be found",
			     source_path);
		return NULL;
	}
	return g_steal_pointer (&source_path);
}

gboolean
fu_uefi_get_framebuffer_size (guint32 *width, guint32 *height, GError **error)
{
	guint32 height_tmp;
	guint32 width_tmp;
	g_autofree gchar *sysfsdriverdir = NULL;
	g_autofree gchar *fbdir = NULL;

	sysfsdriverdir = fu_common_get_path (FU_PATH_KIND_SYSFSDIR_DRIVERS);
	fbdir = g_build_filename (sysfsdriverdir, "efi-framebuffer", "efi-framebuffer.0", NULL);
	if (!g_file_test (fbdir, G_FILE_TEST_EXISTS)) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "EFI framebuffer not found");
		return FALSE;
	}
	height_tmp = fu_uefi_read_file_as_uint64 (fbdir, "height");
	width_tmp = fu_uefi_read_file_as_uint64 (fbdir, "width");
	if (width_tmp == 0 || height_tmp == 0) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "EFI framebuffer has invalid size "
			     "%"G_GUINT32_FORMAT"x%"G_GUINT32_FORMAT,
			     width_tmp, height_tmp);
		return FALSE;
	}
	if (width != NULL)
		*width = width_tmp;
	if (height != NULL)
		*height = height_tmp;
	return TRUE;
}

gboolean
fu_uefi_get_bitmap_size (const guint8 *buf,
			 gsize bufsz,
			 guint32 *width,
			 guint32 *height,
			 GError **error)
{
	guint32 ui32;

	g_return_val_if_fail (buf != NULL, FALSE);

	/* check header */
	if (bufsz < 26) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "blob was too small %" G_GSIZE_FORMAT, bufsz);
		return FALSE;
	}
	if (memcmp (buf, "BM", 2) != 0) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "invalid BMP header signature");
		return FALSE;
	}

	/* starting address */
	ui32 = fu_common_read_uint32 (buf + 10, G_LITTLE_ENDIAN);
	if (ui32 < 26) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "BMP header invalid @ %"G_GUINT32_FORMAT"x", ui32);
		return FALSE;
	}

	/* BITMAPINFOHEADER header */
	ui32 = fu_common_read_uint32 (buf + 14, G_LITTLE_ENDIAN);
	if (ui32 < 26 - 14) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "BITMAPINFOHEADER invalid @ %"G_GUINT32_FORMAT"x", ui32);
		return FALSE;
	}

	/* dimensions */
	if (width != NULL)
		*width = fu_common_read_uint32 (buf + 18, G_LITTLE_ENDIAN);
	if (height != NULL)
		*height = fu_common_read_uint32 (buf + 22, G_LITTLE_ENDIAN);
	return TRUE;
}

gboolean
fu_uefi_secure_boot_enabled (void)
{
	gint rc;
	gsize data_size = 0;
	guint32 attributes = 0;
	g_autofree guint8 *data = NULL;

	rc = efi_get_variable (efi_guid_global, "SecureBoot", &data, &data_size, &attributes);
	if (rc < 0)
		return FALSE;
	if (data_size >= 1 && data[0] & 1)
		return TRUE;
	return FALSE;
}

static gint
fu_uefi_strcmp_sort_cb (gconstpointer a, gconstpointer b)
{
	const gchar *stra = *((const gchar **) a);
	const gchar *strb = *((const gchar **) b);
	return g_strcmp0 (stra, strb);
}

GPtrArray *
fu_uefi_get_esrt_entry_paths (const gchar *esrt_path, GError **error)
{
	GPtrArray *entries = g_ptr_array_new_with_free_func (g_free);
	const gchar *fn;
	g_autofree gchar *esrt_entries = NULL;
	g_autoptr(GDir) dir = NULL;

	/* search ESRT */
	esrt_entries = g_build_filename (esrt_path, "entries", NULL);
	dir = g_dir_open (esrt_entries, 0, error);
	if (dir == NULL)
		return NULL;
	while ((fn = g_dir_read_name (dir)) != NULL)
		g_ptr_array_add (entries, g_build_filename (esrt_entries, fn, NULL));

	/* sort by name */
	g_ptr_array_sort (entries, fu_uefi_strcmp_sort_cb);
	return entries;
}

gchar *
fu_uefi_get_full_esp_path (const gchar *esp_mount)
{
	const gchar *os_release_id = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GHashTable) os_release = fwupd_get_os_release (&error_local);
	if (os_release != NULL) {
		os_release_id = g_hash_table_lookup (os_release, "ID");
	} else {
		g_debug ("failed to get ID: %s", error_local->message);
	}
	if (os_release_id == NULL)
		os_release_id = "unknown";
	return g_build_filename (esp_mount, "EFI", os_release_id, NULL);
}

guint64
fu_uefi_read_file_as_uint64 (const gchar *path, const gchar *attr_name)
{
	g_autofree gchar *data = NULL;
	g_autofree gchar *fn = g_build_filename (path, attr_name, NULL);
	if (!g_file_get_contents (fn, &data, NULL, NULL))
		return 0x0;
	if (g_str_has_prefix (data, "0x"))
		return g_ascii_strtoull (data + 2, NULL, 16);
	return g_ascii_strtoull (data, NULL, 10);
}

gboolean
fu_uefi_prefix_efi_errors (GError **error)
{
	g_autoptr(GString) str = g_string_new (NULL);
	gint rc = 1;
	for (gint i = 0; rc > 0; i++) {
		gchar *filename = NULL;
		gchar *function = NULL;
		gchar *message = NULL;
		gint line = 0;
		gint err = 0;

		rc = efi_error_get (i, &filename, &function, &line,
				    &message, &err);
		if (rc <= 0)
			break;
		g_string_append_printf (str, "{error #%d} %s:%d %s(): %s: %s\t",
					i, filename, line, function,
					message, strerror (err));
	}
	if (str->len > 1)
		g_string_truncate (str, str->len - 1);
	g_prefix_error (error, "%s: ", str->str);
	return FALSE;
}
