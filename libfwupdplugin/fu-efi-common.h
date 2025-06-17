/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-firmware.h"

#define FU_EFI_VOLUME_GUID_FFS1	       "7a9354d9-0468-444a-81ce-0bf617d890df"
#define FU_EFI_VOLUME_GUID_FFS2	       "8c8ce578-8a3d-4f1c-9935-896185c32dd3"
#define FU_EFI_VOLUME_GUID_FFS3	       "5473c07a-3dcb-4dca-bd6f-1e9689e7349a"
#define FU_EFI_VOLUME_GUID_NVRAM_EVSA  "fff12b8d-7696-4c8b-a985-2747075b4f50"
#define FU_EFI_VOLUME_GUID_NVRAM_NVAR  "cef5b9a3-476d-497f-9fdc-e98143e0422c"
#define FU_EFI_VOLUME_GUID_NVRAM_EVSA2 "00504624-8a59-4eeb-bd0f-6b36e96128e0"
#define FU_EFI_VOLUME_GUID_APPLE_BOOT  "04adeead-61ff-4d31-b6ba-64f8bf901f5a"
#define FU_EFI_VOLUME_GUID_PFH1	       "16b45da2-7d70-4aea-a58d-760e9ecb841d"
#define FU_EFI_VOLUME_GUID_PFH2	       "e360bdba-c3ce-46be-8f37-b231e5cb9f35"
#define FU_EFI_VOLUME_GUID_HP_FS       "372b56df-cc9f-4817-ab97-0a10a92ceaa5"

#define FU_EFI_FILE_GUID_FV_IMAGE   "4e35fd93-9c72-4c15-8c4b-e77f1db2d792"
#define FU_EFI_FILE_GUID_MICROCODE  "197db236-f856-4924-90f8-cdf12fb875f3"
#define FU_EFI_FILE_GUID_BIOS_GUARD "7934156d-cfce-460e-92f5-a07909a59eca"

#define FU_EFI_SECTION_GUID_LZMA_COMPRESS  "ee4e5898-3914-4259-9d6e-dc7bd79403cf"
#define FU_EFI_SECTION_GUID_TIANO_COMPRESS "a31280ad-481e-41b6-95e8-127f4c984779"
#define FU_EFI_SECTION_GUID_SMBIOS_TABLE   "eb9d2d31-2d88-11d3-9a16-0090273fc14d"
#define FU_EFI_SECTION_GUID_ESRT_TABLE	   "b122a263-3661-4f68-9929-78f8b0d62180"
#define FU_EFI_SECTION_GUID_ACPI1_TABLE	   "eb9d2d30-2d88-11d3-9a16-0090273fc14d"
#define FU_EFI_SECTION_GUID_ACPI2_TABLE	   "8868e871-e4f1-11d3-bc22-0080c73c8881"

const gchar *
fu_efi_guid_to_name(const gchar *guid);
gboolean
fu_efi_parse_sections(FuFirmware *firmware,
		      GInputStream *stream,
		      gsize offset,
		      FuFirmwareParseFlags flags,
		      GError **error) G_GNUC_NON_NULL(1, 2);
