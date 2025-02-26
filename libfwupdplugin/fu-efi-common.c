/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-bytes.h"
#include "fu-common.h"
#include "fu-efi-common.h"
#include "fu-efi-section.h"
#include "fu-input-stream.h"
#include "fu-lzma-common.h"
#include "fu-partial-input-stream.h"

/**
 * fu_efi_guid_to_name:
 * @guid: A lowercase GUID string, e.g. `8c8ce578-8a3d-4f1c-9935-896185c32dd3`
 *
 * Converts a GUID to the known nice name.
 *
 * Returns: identifier string, or %NULL if unknown
 *
 * Since: 1.6.2
 **/
const gchar *
fu_efi_guid_to_name(const gchar *guid)
{
	if (g_strcmp0(guid, FU_EFI_VOLUME_GUID_FFS1) == 0)
		return "Volume:Ffs1";
	if (g_strcmp0(guid, FU_EFI_VOLUME_GUID_FFS2) == 0)
		return "Volume:Ffs2";
	if (g_strcmp0(guid, FU_EFI_VOLUME_GUID_FFS3) == 0)
		return "Volume:Ffs3";
	if (g_strcmp0(guid, FU_EFI_VOLUME_GUID_NVRAM_EVSA) == 0)
		return "Volume:NvramEvsa";
	if (g_strcmp0(guid, FU_EFI_VOLUME_GUID_NVRAM_NVAR) == 0)
		return "Volume:NvramNvar";
	if (g_strcmp0(guid, FU_EFI_VOLUME_GUID_NVRAM_EVSA2) == 0)
		return "Volume:NvramEvsa2";
	if (g_strcmp0(guid, FU_EFI_VOLUME_GUID_APPLE_BOOT) == 0)
		return "Volume:AppleBoot";
	if (g_strcmp0(guid, FU_EFI_VOLUME_GUID_PFH1) == 0)
		return "Volume:Pfh1";
	if (g_strcmp0(guid, FU_EFI_VOLUME_GUID_PFH2) == 0)
		return "Volume:Pfh2";
	if (g_strcmp0(guid, FU_EFI_VOLUME_GUID_HP_FS) == 0)
		return "Volume:HpFs";
	if (g_strcmp0(guid, FU_EFI_FILE_GUID_FV_IMAGE) == 0)
		return "File:FvImage";
	if (g_strcmp0(guid, FU_EFI_FILE_GUID_MICROCODE) == 0)
		return "File:Microcode";
	if (g_strcmp0(guid, FU_EFI_FILE_GUID_BIOS_GUARD) == 0)
		return "File:BiosGuard";
	if (g_strcmp0(guid, FU_EFI_SECTION_GUID_LZMA_COMPRESS) == 0)
		return "Section:LzmaCompress";
	if (g_strcmp0(guid, FU_EFI_SECTION_GUID_TIANO_COMPRESS) == 0)
		return "Section:TianoCompress";
	if (g_strcmp0(guid, FU_EFI_SECTION_GUID_SMBIOS_TABLE) == 0)
		return "Section:SmbiosTable";
	if (g_strcmp0(guid, FU_EFI_SECTION_GUID_ESRT_TABLE) == 0)
		return "Section:EsrtTable";
	if (g_strcmp0(guid, FU_EFI_SECTION_GUID_ACPI1_TABLE) == 0)
		return "Section:Acpi1Table";
	if (g_strcmp0(guid, FU_EFI_SECTION_GUID_ACPI2_TABLE) == 0)
		return "Section:Acpi2Table";
	return NULL;
}
/**
 * fu_efi_parse_sections:
 * @firmware: #FuFirmware
 * @stream: a #GInputStream
 * @flags: flags
 * @error: (nullable): optional return location for an error
 *
 * Parses a UEFI section.
 *
 * Returns: %TRUE for success
 *
 * Since: 2.0.0
 **/
gboolean
fu_efi_parse_sections(FuFirmware *firmware,
		      GInputStream *stream,
		      gsize offset,
		      FwupdInstallFlags flags,
		      GError **error)
{
	gsize streamsz = 0;

	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	while (offset < streamsz) {
		g_autoptr(FuFirmware) img = fu_efi_section_new();
		g_autoptr(GInputStream) partial_stream = NULL;

		/* parse maximum payload */
		partial_stream =
		    fu_partial_input_stream_new(stream, offset, streamsz - offset, error);
		if (partial_stream == NULL) {
			g_prefix_error(error, "failed to cut payload: ");
			return FALSE;
		}
		if (!fu_firmware_parse_stream(img,
					      partial_stream,
					      0x0,
					      flags | FWUPD_INSTALL_FLAG_NO_SEARCH,
					      error)) {
			g_prefix_error(error,
				       "failed to parse section of size 0x%x: ",
				       (guint)streamsz);
			return FALSE;
		}

		/* invalid */
		if (fu_firmware_get_size(img) == 0) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "section had zero size");
			return FALSE;
		}

		fu_firmware_set_offset(img, offset);
		if (!fu_firmware_add_image_full(firmware, img, error))
			return FALSE;

		/* next! */
		offset += fu_common_align_up(fu_firmware_get_size(img), FU_FIRMWARE_ALIGNMENT_4);
	}

	/* success */
	return TRUE;
}
