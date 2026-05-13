/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-firmware.h"

#define FU_TYPE_EFI_FILE (fu_efi_file_get_type())
G_DECLARE_DERIVABLE_TYPE(FuEfiFile, fu_efi_file, FU, EFI_FILE, FuFirmware)

struct _FuEfiFileClass {
	FuFirmwareClass parent_class;
};

FuFirmware *
fu_efi_file_new(void);
