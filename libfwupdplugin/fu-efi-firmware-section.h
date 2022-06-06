/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-firmware.h"

#define FU_TYPE_EFI_FIRMWARE_SECTION (fu_efi_firmware_section_get_type())
G_DECLARE_DERIVABLE_TYPE(FuEfiFirmwareSection,
			 fu_efi_firmware_section,
			 FU,
			 EFI_FIRMWARE_SECTION,
			 FuFirmware)

struct _FuEfiFirmwareSectionClass {
	FuFirmwareClass parent_class;
};

FuFirmware *
fu_efi_firmware_section_new(void);
