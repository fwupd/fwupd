/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-uefi-dbx-common.h"

static gchar *
fu_uefi_dbx_get_authenticode_hash(const gchar *fn, GError **error)
{
	g_autoptr(FuFirmware) firmware = fu_pefile_firmware_new();
	g_autoptr(GFile) file = g_file_new_for_path(fn);
	if (!fu_firmware_parse_file(firmware, file, FWUPD_INSTALL_FLAG_NONE, error))
		return NULL;
	return fu_firmware_get_checksum(firmware, G_CHECKSUM_SHA256, error);
}

static gboolean
fu_uefi_dbx_signature_list_validate_filename(FuContext *ctx,
					     FuEfiSignatureList *siglist,
					     const gchar *fn,
					     FwupdInstallFlags flags,
					     GError **error)
{
	g_autofree gchar *checksum = NULL;
	g_autoptr(FuFirmware) img = NULL;
	g_autoptr(GError) error_local = NULL;

	/* get checksum of file */
	checksum = fu_uefi_dbx_get_authenticode_hash(fn, &error_local);
	if (checksum == NULL) {
		g_debug("failed to get checksum for %s: %s", fn, error_local->message);
		return TRUE;
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

	/* success */
	return TRUE;
}

gboolean
fu_uefi_dbx_signature_list_validate(FuContext *ctx,
				    FuEfiSignatureList *siglist,
				    FwupdInstallFlags flags,
				    GError **error)
{
	g_autoptr(GPtrArray) files = NULL;

	files = fu_context_get_esp_files(ctx,
					 FU_CONTEXT_ESP_FILE_FLAG_INCLUDE_FIRST_STAGE |
					     FU_CONTEXT_ESP_FILE_FLAG_INCLUDE_SECOND_STAGE,
					 error);
	if (files == NULL)
		return FALSE;
	for (guint i = 0; i < files->len; i++) {
		FuFirmware *firmware = g_ptr_array_index(files, i);
		if (!fu_uefi_dbx_signature_list_validate_filename(
			ctx,
			siglist,
			fu_firmware_get_filename(firmware),
			flags,
			error))
			return FALSE;
	}
	return TRUE;
}
