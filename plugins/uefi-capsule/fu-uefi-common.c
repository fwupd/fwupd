/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2015 Peter Jones <pjones@redhat.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-uefi-common.h"
#include "fu-uefi-device.h"

const gchar *
fu_uefi_bootmgr_get_suffix(GError **error)
{
	guint64 firmware_bits;
	struct {
		guint64 bits;
		const gchar *arch;
	} suffixes[] = {
#if defined(__x86_64__)
		{64, "x64"},
#elif defined(__aarch64__)
		{64, "aa64"},
#endif
#if defined(__x86_64__) || defined(__i386__) || defined(__i686__)
		{32, "ia32"},
#endif
		{0, NULL}
	};
	g_autofree gchar *sysfsfwdir = fu_path_from_kind(FU_PATH_KIND_SYSFSDIR_FW);
	g_autofree gchar *sysfsefidir = g_build_filename(sysfsfwdir, "efi", NULL);
	firmware_bits = fu_uefi_read_file_as_uint64(sysfsefidir, "fw_platform_size");
	if (firmware_bits == 0) {
		g_set_error(error,
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
	g_set_error(error,
		    G_IO_ERROR,
		    G_IO_ERROR_NOT_FOUND,
		    "%s/fw_platform_size has unknown value %" G_GUINT64_FORMAT,
		    sysfsefidir,
		    firmware_bits);
	return NULL;
}

/* return without the ESP dir prepended */
gchar *
fu_uefi_get_esp_app_path(const gchar *cmd, GError **error)
{
	const gchar *suffix = fu_uefi_bootmgr_get_suffix(error);
	g_autofree gchar *base = NULL;
	if (suffix == NULL)
		return NULL;
	base = fu_uefi_get_esp_path_for_os();
	return g_strdup_printf("%s/%s%s.efi", base, cmd, suffix);
}

/**
 * fu_uefi_get_built_app_path:
 * @basename: the prefix for the binary
 * @error: (nullable): optional return location for an error
 *
 * Gets the path intended to be used for an EFI binary on the local system.
 * The binary is matched against the correct architecture and if secure
 * boot is enabled.
 *
 * Returns: The full path to the binary, or %NULL if not found
 *
 * Since: 1.8.1
 **/
gchar *
fu_uefi_get_built_app_path(const gchar *binary, GError **error)
{
	const gchar *suffix;
	g_autofree gchar *prefix = NULL;
	g_autofree gchar *source_path = NULL;
	g_autofree gchar *source_path_signed = NULL;
	gboolean source_path_exists = FALSE;
	gboolean source_path_signed_exists = FALSE;

	suffix = fu_uefi_bootmgr_get_suffix(error);
	if (suffix == NULL)
		return NULL;
	prefix = fu_path_from_kind(FU_PATH_KIND_EFIAPPDIR);

	source_path = g_strdup_printf("%s/%s%s.efi", prefix, binary, suffix);
	source_path_signed = g_strdup_printf("%s.signed", source_path);

	source_path_exists = g_file_test(source_path, G_FILE_TEST_EXISTS);
	source_path_signed_exists = g_file_test(source_path_signed, G_FILE_TEST_EXISTS);

	if (fu_efivar_secure_boot_enabled(NULL)) {
		if (!source_path_signed_exists) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_FOUND,
				    "%s cannot be found",
				    source_path_signed);
			return NULL;
		}
		return g_steal_pointer(&source_path_signed);
	}

	if (source_path_exists)
		return g_steal_pointer(&source_path);
	if (source_path_signed_exists)
		return g_steal_pointer(&source_path_signed);

	g_set_error(error,
		    G_IO_ERROR,
		    G_IO_ERROR_NOT_FOUND,
		    "%s and %s cannot be found",
		    source_path,
		    source_path_signed);
	return NULL;
}

gboolean
fu_uefi_get_framebuffer_size(guint32 *width, guint32 *height, GError **error)
{
	guint32 height_tmp;
	guint32 width_tmp;
	g_autofree gchar *sysfsdriverdir = NULL;
	g_autofree gchar *fbdir = NULL;

	sysfsdriverdir = fu_path_from_kind(FU_PATH_KIND_SYSFSDIR_DRIVERS);
	fbdir = g_build_filename(sysfsdriverdir, "efi-framebuffer", "efi-framebuffer.0", NULL);
	if (!g_file_test(fbdir, G_FILE_TEST_EXISTS)) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "EFI framebuffer not found");
		return FALSE;
	}
	height_tmp = fu_uefi_read_file_as_uint64(fbdir, "height");
	width_tmp = fu_uefi_read_file_as_uint64(fbdir, "width");
	if (width_tmp == 0 || height_tmp == 0) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "EFI framebuffer has invalid size "
			    "%" G_GUINT32_FORMAT "x%" G_GUINT32_FORMAT,
			    width_tmp,
			    height_tmp);
		return FALSE;
	}
	if (width != NULL)
		*width = width_tmp;
	if (height != NULL)
		*height = height_tmp;
	return TRUE;
}

gboolean
fu_uefi_get_bitmap_size(const guint8 *buf,
			gsize bufsz,
			guint32 *width,
			guint32 *height,
			GError **error)
{
	guint32 ui32;

	g_return_val_if_fail(buf != NULL, FALSE);

	/* check header */
	if (bufsz < 26 || memcmp(buf, "BM", 2) != 0) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "invalid BMP header signature");
		return FALSE;
	}

	/* starting address */
	if (!fu_memread_uint32_safe(buf, bufsz, 10, &ui32, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (ui32 < 26) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "BMP header invalid @ %" G_GUINT32_FORMAT "x",
			    ui32);
		return FALSE;
	}

	/* BITMAPINFOHEADER header */
	if (!fu_memread_uint32_safe(buf, bufsz, 14, &ui32, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (ui32 < 26 - 14) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "BITMAPINFOHEADER invalid @ %" G_GUINT32_FORMAT "x",
			    ui32);
		return FALSE;
	}

	/* dimensions */
	if (width != NULL) {
		if (!fu_memread_uint32_safe(buf, bufsz, 18, width, G_LITTLE_ENDIAN, error))
			return FALSE;
	}
	if (height != NULL) {
		if (!fu_memread_uint32_safe(buf, bufsz, 22, height, G_LITTLE_ENDIAN, error))
			return FALSE;
	}
	return TRUE;
}

/* return without the ESP dir prepended */
gchar *
fu_uefi_get_esp_path_for_os(void)
{
#ifndef EFI_OS_DIR
	const gchar *os_release_id = NULL;
	const gchar *id_like = NULL;
	g_autofree gchar *esp_path = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GHashTable) os_release = fwupd_get_os_release(&error_local);
	/* try to lookup /etc/os-release ID key */
	if (os_release != NULL) {
		os_release_id = g_hash_table_lookup(os_release, "ID");
	} else {
		g_debug("failed to get ID: %s", error_local->message);
	}
	if (os_release_id == NULL)
		os_release_id = "unknown";
	/* if ID key points at something existing return it */
	esp_path = g_build_filename("EFI", os_release_id, NULL);
	if (g_file_test(esp_path, G_FILE_TEST_IS_DIR) || os_release == NULL)
		return g_steal_pointer(&esp_path);
	/* if ID key doesn't exist, try ID_LIKE */
	id_like = g_hash_table_lookup(os_release, "ID_LIKE");
	if (id_like != NULL) {
		g_auto(GStrv) split = g_strsplit(id_like, " ", -1);
		for (guint i = 0; split[i] != NULL; i++) {
			g_autofree gchar *id_like_path = g_build_filename("EFI", split[i], NULL);
			if (g_file_test(id_like_path, G_FILE_TEST_IS_DIR)) {
				g_debug("using ID_LIKE key from os-release");
				return g_steal_pointer(&id_like_path);
			}
		}
	}
	return g_steal_pointer(&esp_path);
#else
	return g_build_filename("EFI", EFI_OS_DIR, NULL);
#endif
}

guint64
fu_uefi_read_file_as_uint64(const gchar *path, const gchar *attr_name)
{
	guint64 tmp = 0;
	g_autofree gchar *data = NULL;
	g_autofree gchar *fn = g_build_filename(path, attr_name, NULL);
	g_autoptr(GError) error_local = NULL;

	if (!g_file_get_contents(fn, &data, NULL, NULL))
		return 0x0;
	if (!fu_strtoull(data, &tmp, 0, G_MAXUINT64, &error_local)) {
		g_warning("invalid string specified: %s", error_local->message);
		return G_MAXUINT64;
	}
	return tmp;
}

gboolean
fu_uefi_esp_target_verify(const gchar *source_fn, FuVolume *esp, const gchar *target_no_mountpoint)
{
	gsize len = 0;
	g_autofree gchar *source_csum = NULL;
	g_autofree gchar *source_data = NULL;
	g_autofree gchar *target_csum = NULL;
	g_autofree gchar *target_data = NULL;
	g_autofree gchar *esp_path = fu_volume_get_mount_point(esp);
	g_autofree gchar *target_fn = g_build_filename(esp_path, target_no_mountpoint, NULL);

	/* nothing in target yet */
	if (!g_file_test(target_fn, G_FILE_TEST_EXISTS))
		return FALSE;

	/* test if the file needs to be updated */
	if (!g_file_get_contents(source_fn, &source_data, &len, NULL))
		return FALSE;
	source_csum = g_compute_checksum_for_data(G_CHECKSUM_SHA256, (guchar *)source_data, len);
	if (!g_file_get_contents(target_fn, &target_data, &len, NULL))
		return FALSE;
	target_csum = g_compute_checksum_for_data(G_CHECKSUM_SHA256, (guchar *)target_data, len);
	return g_strcmp0(target_csum, source_csum) == 0;
}

gboolean
fu_uefi_esp_target_exists(FuVolume *esp, const gchar *target_no_mountpoint)
{
	g_autofree gchar *esp_path = fu_volume_get_mount_point(esp);
	g_autofree gchar *target_fn = g_build_filename(esp_path, target_no_mountpoint, NULL);
	return g_file_test(target_fn, G_FILE_TEST_EXISTS);
}

gboolean
fu_uefi_esp_target_copy(const gchar *source_fn,
			FuVolume *esp,
			const gchar *target_no_mountpoint,
			GError **error)
{
	g_autofree gchar *esp_path = fu_volume_get_mount_point(esp);
	g_autofree gchar *target_fn = g_build_filename(esp_path, target_no_mountpoint, NULL);
	g_autoptr(GFile) source_file = g_file_new_for_path(source_fn);
	g_autoptr(GFile) target_file = g_file_new_for_path(target_fn);

	if (!g_file_copy(source_file,
			 target_file,
			 G_FILE_COPY_OVERWRITE,
			 NULL,
			 NULL,
			 NULL,
			 error)) {
		g_prefix_error(error, "Failed to copy %s to %s: ", source_fn, target_fn);
		return FALSE;
	}

	return TRUE;
}
