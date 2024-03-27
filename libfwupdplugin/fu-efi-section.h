/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-firmware.h"

#define FU_TYPE_EFI_SECTION (fu_efi_section_get_type())
G_DECLARE_DERIVABLE_TYPE(FuEfiSection, fu_efi_section, FU, EFI_SECTION, FuFirmware)

struct _FuEfiSectionClass {
	FuFirmwareClass parent_class;
};

FuFirmware *
fu_efi_section_new(void);
