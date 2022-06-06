/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-efi-common.h"

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
	if (g_strcmp0(guid, FU_EFI_FIRMWARE_VOLUME_GUID_FFS1) == 0)
		return "Volume:Ffs1";
	if (g_strcmp0(guid, FU_EFI_FIRMWARE_VOLUME_GUID_FFS2) == 0)
		return "Volume:Ffs2";
	if (g_strcmp0(guid, FU_EFI_FIRMWARE_VOLUME_GUID_FFS3) == 0)
		return "Volume:Ffs3";
	if (g_strcmp0(guid, FU_EFI_FIRMWARE_VOLUME_GUID_NVRAM_EVSA) == 0)
		return "Volume:NvramEvsa";
	if (g_strcmp0(guid, FU_EFI_FIRMWARE_VOLUME_GUID_NVRAM_NVAR) == 0)
		return "Volume:NvramNvar";
	if (g_strcmp0(guid, FU_EFI_FIRMWARE_VOLUME_GUID_NVRAM_EVSA2) == 0)
		return "Volume:NvramEvsa2";
	if (g_strcmp0(guid, FU_EFI_FIRMWARE_VOLUME_GUID_APPLE_BOOT) == 0)
		return "Volume:AppleBoot";
	if (g_strcmp0(guid, FU_EFI_FIRMWARE_VOLUME_GUID_PFH1) == 0)
		return "Volume:Pfh1";
	if (g_strcmp0(guid, FU_EFI_FIRMWARE_VOLUME_GUID_PFH2) == 0)
		return "Volume:Pfh2";
	if (g_strcmp0(guid, FU_EFI_FIRMWARE_FILE_FV_IMAGE) == 0)
		return "File:FvImage";
	if (g_strcmp0(guid, FU_EFI_FIRMWARE_FILE_MICROCODE) == 0)
		return "File:Microcode";
	if (g_strcmp0(guid, FU_EFI_FIRMWARE_FILE_BIOS_GUARD) == 0)
		return "File:BiosGuard";
	if (g_strcmp0(guid, FU_EFI_FIRMWARE_SECTION_LZMA_COMPRESS) == 0)
		return "Section:LzmaCompress";
	if (g_strcmp0(guid, FU_EFI_FIRMWARE_SECTION_TIANO_COMPRESS) == 0)
		return "Section:TianoCompress";
	if (g_strcmp0(guid, FU_EFI_FIRMWARE_SECTION_TIANO_COMPRESS) == 0)
		return "Section:TianoCompress";
	if (g_strcmp0(guid, FU_EFI_FIRMWARE_SECTION_SMBIOS_TABLE) == 0)
		return "Section:SmbiosTable";
	if (g_strcmp0(guid, FU_EFI_FIRMWARE_SECTION_ESRT_TABLE) == 0)
		return "Section:EsrtTable";
	if (g_strcmp0(guid, FU_EFI_FIRMWARE_SECTION_ACPI1_TABLE) == 0)
		return "Section:Acpi1Table";
	if (g_strcmp0(guid, FU_EFI_FIRMWARE_SECTION_ACPI2_TABLE) == 0)
		return "Section:Acpi2Table";
	return NULL;
}
