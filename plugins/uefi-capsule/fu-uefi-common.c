/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2015 Peter Jones <pjones@redhat.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <efivar.h>

#include "fu-common.h"
#include "fu-uefi-common.h"
#include "fu-efivar.h"

#include "fwupd-common.h"
#include "fwupd-error.h"

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
fu_uefi_get_esp_app_path (FuDevice *device,
			   const gchar *esp_path,
			   const gchar *cmd,
			   GError **error)
{
	const gchar *suffix = fu_uefi_bootmgr_get_suffix (error);
	g_autofree gchar *base = NULL;
	if (suffix == NULL)
		return NULL;
	base = fu_uefi_get_esp_path_for_os (device, esp_path);
	return g_strdup_printf ("%s/%s%s.efi", base, cmd, suffix);
}

gchar *
fu_uefi_get_built_app_path (GError **error)
{
	const gchar *extension = "";
	const gchar *suffix;
	g_autofree gchar *source_path = NULL;
	g_autofree gchar *prefix = NULL;
	if (fu_efivar_secure_boot_enabled ())
		extension = ".signed";
	suffix = fu_uefi_bootmgr_get_suffix (error);
	if (suffix == NULL)
		return NULL;
	prefix = fu_common_get_path (FU_PATH_KIND_EFIAPPDIR);
	source_path = g_strdup_printf ("%s/fwupd%s.efi%s",
				       prefix,
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
	if (bufsz < 26 || memcmp (buf, "BM", 2) != 0) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "invalid BMP header signature");
		return FALSE;
	}

	/* starting address */
	if (!fu_common_read_uint32_safe (buf, bufsz, 10, &ui32, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (ui32 < 26) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "BMP header invalid @ %"G_GUINT32_FORMAT"x", ui32);
		return FALSE;
	}

	/* BITMAPINFOHEADER header */
	if (!fu_common_read_uint32_safe (buf, bufsz, 14, &ui32, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (ui32 < 26 - 14) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "BITMAPINFOHEADER invalid @ %"G_GUINT32_FORMAT"x", ui32);
		return FALSE;
	}

	/* dimensions */
	if (width != NULL) {
		if (!fu_common_read_uint32_safe (buf, bufsz, 18, width,
						 G_LITTLE_ENDIAN, error))
			return FALSE;
	}
	if (height != NULL) {
		if (!fu_common_read_uint32_safe (buf, bufsz, 22, height,
						 G_LITTLE_ENDIAN, error))
			return FALSE;
	}
	return TRUE;
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
fu_uefi_get_esp_path_for_os (FuDevice *device, const gchar *base)
{
#ifndef EFI_OS_DIR
	const gchar *os_release_id = NULL;
	const gchar *id_like_id = NULL;
	g_autofree gchar *esp_path = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GHashTable) os_release = fwupd_get_os_release (&error_local);
	/* try to lookup /etc/os-release ID key */
	if (os_release != NULL) {
		os_release_id = g_hash_table_lookup (os_release, "ID");
	} else {
		g_debug ("failed to get ID: %s", error_local->message);
	}
	if (os_release_id == NULL)
		os_release_id = "unknown";
	/* if ID key points at something existing return it */
	esp_path = g_build_filename (base, "EFI", os_release_id, NULL);
	if (g_file_test (esp_path, G_FILE_TEST_IS_DIR) || os_release == NULL)
		return g_steal_pointer (&esp_path);
	/* if ID key doesn't exist, try ID_LIKE */
	id_like_id = g_hash_table_lookup (os_release, "ID_LIKE");
	if (id_like_id != NULL) {
		g_autofree gchar* id_like_path = g_build_filename (base, "EFI", id_like_id, NULL);
		if (g_file_test (id_like_path, G_FILE_TEST_IS_DIR)) {
			g_debug ("Using ID_LIKE key from os-release");
			return g_steal_pointer (&id_like_path);
		}
	} 
	/* try to fallback to use UEFI removable path if ID_LIKE path doesn't exist */
	if (fu_device_get_metadata_boolean (device, "FallbacktoRemovablePath")) {
		esp_path = g_build_filename (base, "EFI", "boot", NULL);
		if (!g_file_test (esp_path, G_FILE_TEST_IS_DIR))
			g_debug ("failed to fallback due to missing %s", esp_path);
	}
	return g_steal_pointer (&esp_path);
#else
	return g_build_filename (base, "EFI", EFI_OS_DIR, NULL);
#endif
}

guint64
fu_uefi_read_file_as_uint64 (const gchar *path, const gchar *attr_name)
{
	g_autofree gchar *data = NULL;
	g_autofree gchar *fn = g_build_filename (path, attr_name, NULL);
	if (!g_file_get_contents (fn, &data, NULL, NULL))
		return 0x0;
	return fu_common_strtoull (data);
}
