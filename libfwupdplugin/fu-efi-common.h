/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <gio/gio.h>

#define FU_EFI_FIRMWARE_VOLUME_GUID_FFS1	"7a9354d9-0468-444a-81ce-0bf617d890df"
#define FU_EFI_FIRMWARE_VOLUME_GUID_FFS2	"8c8ce578-8a3d-4f1c-9935-896185c32dd3"
#define FU_EFI_FIRMWARE_VOLUME_GUID_FFS3	"5473c07a-3dcb-4dca-bd6f-1e9689e7349a"
#define FU_EFI_FIRMWARE_VOLUME_GUID_NVRAM_EVSA	"fff12b8d-7696-4c8b-a985-2747075b4f50"
#define FU_EFI_FIRMWARE_VOLUME_GUID_NVRAM_NVAR	"cef5b9a3-476d-497f-9fdc-e98143e0422c"
#define FU_EFI_FIRMWARE_VOLUME_GUID_NVRAM_EVSA2	"00504624-8a59-4eeb-bd0f-6b36e96128e0"
#define FU_EFI_FIRMWARE_VOLUME_GUID_APPLE_BOOT	"04adeead-61ff-4d31-b6ba-64f8bf901f5a"
#define FU_EFI_FIRMWARE_VOLUME_GUID_PFH1	"16b45da2-7d70-4aea-a58d-760e9ecb841d"
#define FU_EFI_FIRMWARE_VOLUME_GUID_PFH2	"e360bdba-c3ce-46be-8f37-b231e5cb9f35"

const gchar	*fu_efi_guid_to_name		(const gchar	*guid);
