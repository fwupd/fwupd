/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-efi-image.h"
#include "fu-uefi-dbx-common.h"

gchar *
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
	g_debug("SHA256 was %s", fu_efi_image_get_checksum(img));
	return g_strdup(fu_efi_image_get_checksum(img));
}

static gboolean
fu_uefi_dbx_signature_list_validate_volume(FuEfiSignatureList *siglist,
					   FuVolume *esp,
					   GError **error)
{
	g_autofree gchar *esp_path = NULL;
	g_autoptr(GPtrArray) files = NULL;

	/* get list of files contained in the ESP */
	esp_path = fu_volume_get_mount_point(esp);
	if (esp_path == NULL)
		return TRUE;
	files = fu_path_get_files(esp_path, error);
	if (files == NULL)
		return FALSE;

	/* verify each file does not exist in the ESP */
	for (guint i = 0; i < files->len; i++) {
		const gchar *fn = g_ptr_array_index(files, i);
		g_autofree gchar *checksum = NULL;
		g_autoptr(FuFirmware) img = NULL;
		g_autoptr(GError) error_local = NULL;

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
fu_uefi_dbx_signature_list_validate(FuEfiSignatureList *siglist, GError **error)
{
	g_autoptr(GPtrArray) volumes = NULL;
	volumes = fu_volume_new_by_kind(FU_VOLUME_KIND_ESP, error);
	if (volumes == NULL)
		return FALSE;
	for (guint i = 0; i < volumes->len; i++) {
		FuVolume *esp = g_ptr_array_index(volumes, i);
		g_autoptr(FuDeviceLocker) locker = NULL;
		locker = fu_volume_locker(esp, error);
		if (locker == NULL)
			return FALSE;
		if (!fu_uefi_dbx_signature_list_validate_volume(siglist, esp, error))
			return FALSE;
	}
	return TRUE;
}
