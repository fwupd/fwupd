/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#ifdef HAVE_UTSNAME_H
#include <sys/utsname.h>
#endif

#include "fu-uefi-dbx-common.h"

const gchar *
fu_uefi_dbx_get_efi_arch(void)
{
#ifdef HAVE_UTSNAME_H
	struct utsname name_tmp = {0};
	struct {
		const gchar *arch;
		const gchar *arch_efi;
	} map[] = {
	    {"x86", "ia32"},
	    {"x86_64", "x64"},
	    {"arm", "arm"},
	    {"aarch64", "aa64"},
	    {"loongarch64", "loongarch64"},
	    {"riscv64", "riscv64"},
	};

	if (uname(&name_tmp) < 0)
		return NULL;
	for (guint i = 0; i < G_N_ELEMENTS(map); i++) {
		if (g_strcmp0(name_tmp.machine, map[i].arch) == 0)
			return map[i].arch_efi;
	}
#endif
	return NULL;
}

static gchar *
fu_uefi_dbx_get_authenticode_hash(const gchar *fn, GError **error)
{
	g_autoptr(FuFirmware) firmware = fu_pefile_firmware_new();
	g_autoptr(GFile) file = g_file_new_for_path(fn);
	if (!fu_firmware_parse_file(firmware, file, FU_FIRMWARE_PARSE_FLAG_NONE, error))
		return NULL;
	return fu_firmware_get_checksum(firmware, G_CHECKSUM_SHA256, error);
}

static gboolean
fu_uefi_dbx_signature_list_validate_filename(FuContext *ctx,
					     FuEfiSignatureList *siglist,
					     const gchar *fn,
					     FuFirmwareParseFlags flags,
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
				    FuFirmwareParseFlags flags,
				    GError **error)
{
	g_autoptr(GPtrArray) files = NULL;
	g_autoptr(GError) error_local = NULL;

	files = fu_context_get_esp_files(ctx,
					 FU_CONTEXT_ESP_FILE_FLAG_INCLUDE_FIRST_STAGE |
					     FU_CONTEXT_ESP_FILE_FLAG_INCLUDE_SECOND_STAGE,
					 &error_local);
	if (files == NULL) {
		/* there is no BootOrder in CI */
		if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND))
			return TRUE;
		g_propagate_error(error, g_steal_pointer(&error_local));
		return FALSE;
	}
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
