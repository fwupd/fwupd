/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-efi-image.h"
#include "fu-uefi-dbx-common.h"

static gchar *
fu_uefi_dbx_get_authenticode_hash(const gchar *fn, GError **error)
{
	g_autoptr(FuEfiImage) img = NULL;
	g_autoptr(GBytes) bytes = NULL;
	g_autoptr(GMappedFile) mmap = NULL;

	g_debug("getting Authenticode hash of %s", fn);
	mmap = g_mapped_file_new(fn, FALSE, error);
	if (mmap == NULL)
		return NULL;
	bytes = g_mapped_file_get_bytes(mmap);

	img = fu_efi_image_new(bytes, error);
	if (img == NULL)
		return NULL;
	g_debug("Authenticode hash was %s", fu_efi_image_get_checksum(img));
	return g_strdup(fu_efi_image_get_checksum(img));
}

static GPtrArray *
fu_uefi_dbx_get_basenames_bootxxxx(void)
{
	g_autoptr(GPtrArray) files = g_ptr_array_new_with_free_func(g_free);

	/* hardcoded, as we chainload from shim to these */
	g_ptr_array_add(files, g_strdup_printf("fwupd%s.efi", EFI_MACHINE_TYPE_NAME));
	g_ptr_array_add(files, g_strdup_printf("grub%s.efi", EFI_MACHINE_TYPE_NAME));

	for (guint i = 0; i <= G_MAXUINT16; i++) {
		g_autofree gchar *basename = NULL;
		g_autofree gchar *basename_down = NULL;
		g_autofree gchar *fullpath = NULL;
		g_autofree gchar *name = g_strdup_printf("Boot%04X", i);
		g_autoptr(FuEfiLoadOption) loadopt = NULL;
		g_autoptr(FuFirmware) dp_buf = NULL;
		g_autoptr(FuFirmware) dp_file = NULL;
		g_autoptr(GBytes) blob = NULL;
		g_autoptr(GError) error_local = NULL;

		/* load EFI load option */
		blob = fu_efivar_get_data_bytes(FU_EFIVAR_GUID_EFI_GLOBAL, name, NULL, NULL);
		if (blob == NULL)
			continue;
		loadopt = fu_efi_load_option_new();
		if (!fu_firmware_parse(FU_FIRMWARE(loadopt),
				       blob,
				       FWUPD_INSTALL_FLAG_NONE,
				       &error_local)) {
			g_warning("failed to parse %s: %s", name, error_local->message);
			continue;
		}

		/* get EfiFilePath from the list of DEVICE PATHs */
		dp_buf = fu_firmware_get_image_by_gtype(FU_FIRMWARE(loadopt),
							FU_TYPE_EFI_DEVICE_PATH_LIST,
							&error_local);
		if (dp_buf == NULL) {
			g_warning("no DP list for %s: %s", name, error_local->message);
			continue;
		}
		dp_file = fu_firmware_get_image_by_gtype(dp_buf,
							 FU_TYPE_EFI_FILE_PATH_DEVICE_PATH,
							 &error_local);
		if (dp_file == NULL) {
			g_debug("no EfiFilePathDevicePath list for %s: %s",
				name,
				error_local->message);
			continue;
		}
		fullpath =
		    fu_efi_file_path_device_path_get_name(FU_EFI_FILE_PATH_DEVICE_PATH(dp_file),
							  &error_local);
		if (fullpath == NULL) {
			g_warning("no EfiFilePathDevicePath name for %s: %s",
				  name,
				  error_local->message);
			continue;
		}
		g_debug("found %s from %s", fullpath, name);

		/* add lowercase basename if not already present */
		basename = g_path_get_basename(fullpath);
		basename_down = g_utf8_strdown(basename, -1);
		if (g_ptr_array_find_with_equal_func(files, basename_down, g_str_equal, NULL))
			continue;
		g_ptr_array_add(files, g_steal_pointer(&basename_down));
	}
	return g_steal_pointer(&files);
}

static gboolean
fu_uefi_dbx_signature_list_validate_volume(FuEfiSignatureList *siglist,
					   FuVolume *esp,
					   FwupdInstallFlags flags,
					   GError **error)
{
	g_autofree gchar *esp_path = NULL;
	g_autoptr(GPtrArray) files = NULL;
	g_autoptr(GPtrArray) basenames = NULL;

	/* get list of files contained in the ESP */
	esp_path = fu_volume_get_mount_point(esp);
	if (esp_path == NULL)
		return TRUE;
	files = fu_path_get_files(esp_path, error);
	if (files == NULL)
		return FALSE;

	/* filter the list of possible names from BootXXXX */
	if (flags & FWUPD_INSTALL_FLAG_FORCE) {
		basenames = fu_uefi_dbx_get_basenames_bootxxxx();
		if (basenames->len > 0) {
			g_autofree gchar *str = fu_strjoin(",", basenames);
			g_info("EFI binaries to check: %s", str);
		}
	}

	/* verify each file does not exist in the ESP */
	for (guint i = 0; i < files->len; i++) {
		const gchar *fn = g_ptr_array_index(files, i);
		g_autofree gchar *checksum = NULL;
		g_autoptr(FuFirmware) img = NULL;
		g_autoptr(GError) error_local = NULL;

		/* is listed in the BootXXXX variables */
		if (basenames != NULL && basenames->len > 0) {
			g_autofree gchar *basename = g_path_get_basename(fn);
			g_autofree gchar *basename_down = g_utf8_strdown(basename, -1);
			if (!g_ptr_array_find_with_equal_func(files,
							      basename_down,
							      g_str_equal,
							      NULL)) {
				g_debug("%s was not in a BootXXXX variable", fn);
				continue;
			}
		}

		/* get checksum of file */
		checksum = fu_uefi_dbx_get_authenticode_hash(fn, &error_local);
		if (checksum == NULL) {
			g_debug("failed to get checksum for %s: %s", fn, error_local->message);
			continue;
		}

		/* Authenticode signature is present in dbx! */
		g_debug("fn=%s, checksum=%s", fn, checksum);
		img = fu_firmware_get_image_by_checksum(FU_FIRMWARE(siglist), checksum, NULL);
		if (img != NULL) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NEEDS_USER_ACTION,
				    "%s Authenticode checksum [%s] is present in dbx",
				    fn,
				    checksum);
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

gboolean
fu_uefi_dbx_signature_list_validate(FuContext *ctx,
				    FuEfiSignatureList *siglist,
				    FwupdInstallFlags flags,
				    GError **error)
{
	g_autoptr(GPtrArray) volumes = NULL;
	volumes = fu_context_get_esp_volumes(ctx, error);
	if (volumes == NULL)
		return FALSE;
	for (guint i = 0; i < volumes->len; i++) {
		FuVolume *esp = g_ptr_array_index(volumes, i);
		g_autoptr(FuDeviceLocker) locker = NULL;
		g_autoptr(GError) error_local = NULL;
		locker = fu_volume_locker(esp, &error_local);
		if (locker == NULL) {
			if (!g_error_matches(error_local, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED)) {
				g_propagate_error(error, g_steal_pointer(&error_local));
				return FALSE;
			}
			g_debug("failed to mount ESP: %s", error_local->message);
			continue;
		}
		if (!fu_uefi_dbx_signature_list_validate_volume(siglist, esp, flags, error))
			return FALSE;
	}
	return TRUE;
}
