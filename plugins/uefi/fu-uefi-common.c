/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2015-2017 Peter Jones <pjones@redhat.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <efivar.h>
#include <gio/gunixmounts.h>

#include "fu-common.h"
#include "fu-uefi-common.h"
#include "fu-uefi-vars.h"
#include "fu-uefi-udisks.h"

#include "fwupd-common.h"
#include "fwupd-error.h"

#ifndef HAVE_GIO_2_55_0
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GUnixMountEntry, g_unix_mount_free)
#pragma clang diagnostic pop
#endif

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
fu_uefi_get_esp_app_path (const gchar *esp_path, const gchar *cmd, GError **error)
{
	const gchar *suffix = fu_uefi_bootmgr_get_suffix (error);
	g_autofree gchar *base = NULL;
	if (suffix == NULL)
		return NULL;
	base = fu_uefi_get_esp_path_for_os (esp_path);
	return g_strdup_printf ("%s/%s%s.efi", base, cmd, suffix);
}

gchar *
fu_uefi_get_built_app_path (GError **error)
{
	const gchar *extension = "";
	const gchar *suffix;
	g_autofree gchar *source_path = NULL;
	g_autofree gchar *prefix = NULL;
	if (fu_uefi_secure_boot_enabled ())
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
	gsize data_size = 0;
	g_autofree guint8 *data = NULL;

	if (!fu_uefi_vars_get_data (FU_UEFI_VARS_GUID_EFI_GLOBAL, "SecureBoot",
				    &data, &data_size, NULL, NULL))
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
fu_uefi_get_esp_path_for_os (const gchar *base)
{
#ifndef EFI_OS_DIR
	const gchar *os_release_id = NULL;
	const gchar *id_like_id;
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

gboolean
fu_uefi_check_esp_free_space (const gchar *path, guint64 required, GError **error)
{
	guint64 fs_free;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GFileInfo) info = NULL;

	/* skip the checks for unmounted disks */
	if (fu_uefi_udisks_objpath (path))
		return TRUE;

	file = g_file_new_for_path (path);
	info = g_file_query_filesystem_info (file,
					     G_FILE_ATTRIBUTE_FILESYSTEM_FREE,
					     NULL, error);
	if (info == NULL)
		return FALSE;
	fs_free = g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_FILESYSTEM_FREE);
	if (fs_free < required) {
		g_autofree gchar *str_free = g_format_size (fs_free);
		g_autofree gchar *str_reqd = g_format_size (required);
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "%s does not have sufficient space, required %s, got %s",
			     path, str_reqd, str_free);
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_uefi_check_esp_path (const gchar *path, GError **error)
{
	const gchar *fs_types[] = { "vfat", "ntfs", "exfat", "autofs", NULL };
	g_autoptr(GUnixMountEntry) mount = g_unix_mount_at (path, NULL);
	if (mount == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_FOUND,
			     "%s was not mounted", path);
		return FALSE;
	}

	/* /boot is a special case because systemd sandboxing marks
	 * it read-only, but we need to write to /boot/EFI
	 */
	if (g_strcmp0 (path, "/boot") == 0) {
		if (!g_file_test ("/boot/EFI", G_FILE_TEST_IS_DIR)) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "%s/EFI does not exist", path);
			return FALSE;
		}
	/* /efi is a special case because systemd sandboxing marks
	 * it read-only, but we need to write to /efi/EFI
	 */
	} else if (g_strcmp0 (path, "/efi") == 0) {
		if (!g_file_test ("/efi/EFI", G_FILE_TEST_IS_DIR)) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "%s/EFI does not exist", path);
			return FALSE;
		}
	} else if (g_unix_mount_is_readonly (mount)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "%s is read only", path);
		return FALSE;
	}
	if (!g_strv_contains (fs_types, g_unix_mount_get_fs_type (mount))) {
		g_autofree gchar *supported = g_strjoinv ("|", (gchar **) fs_types);
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "%s has an invalid type, expected %s",
			     path, supported);
		return FALSE;
	}
	return TRUE;
}

static gchar *
fu_uefi_probe_udisks_esp (GError **error)
{
	g_autoptr(GPtrArray) devices = NULL;
	g_autofree gchar *found_esp = NULL;

	devices = fu_uefi_udisks_get_block_devices (error);
	if (devices == NULL)
		return NULL;
	for (guint i = 0; i < devices->len; i++) {
		const gchar *obj = g_ptr_array_index (devices, i);
		gboolean esp = fu_uefi_udisks_objpath_is_esp (obj);
		g_debug ("block device %s, is_esp: %d", obj, esp);
		if (!esp)
			continue;
		if (found_esp != NULL) {
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_INVALID_FILENAME,
					     "Multiple EFI system partitions found, "
					     "See https://github.com/fwupd/fwupd/wiki/Determining-EFI-system-partition-location");
			return NULL;
		}
		found_esp = g_strdup (obj);
	}
	if (found_esp == NULL) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_FILENAME,
				     "Unable to determine EFI system partition location, "
				     "See https://github.com/fwupd/fwupd/wiki/Determining-EFI-system-partition-location");
		return NULL;
	}

	g_debug ("Udisks detected objpath %s", found_esp);
	return g_steal_pointer (&found_esp);
}

gchar *
fu_uefi_guess_esp_path (GError **error)
{
	const gchar *paths[] = {"/boot/efi", "/boot", "/efi", NULL};
	const gchar *path_tmp;

	/* for the test suite use local directory for ESP */
	path_tmp = g_getenv ("FWUPD_UEFI_ESP_PATH");
	if (path_tmp != NULL)
		return g_strdup (path_tmp);

	/* try to use known paths */
	for (guint i = 0; paths[i] != NULL; i++) {
		g_autoptr(GError) error_local = NULL;
		if (!fu_uefi_check_esp_path (paths[i], &error_local)) {
			g_debug ("ignoring ESP path: %s", error_local->message);
			continue;
		}
		return g_strdup (paths[i]);
	}

	/* probe using udisks2 */
	return fu_uefi_probe_udisks_esp (error);
}

void
fu_uefi_print_efivar_errors (void)
{
	for (gint i = 0; ; i++) {
		gchar *filename = NULL;
		gchar *function = NULL;
		gchar *message = NULL;
		gint line = 0;
		gint err = 0;
		if (efi_error_get (i, &filename, &function, &line,
				   &message, &err) <= 0)
			break;
		g_debug ("{efivar error #%d} %s:%d %s(): %s: %s\t",
			 i, filename, line, function,
			 message, strerror (err));
	}
}
