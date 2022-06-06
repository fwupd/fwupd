/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-firmware.h"

#define FU_TYPE_EFI_FIRMWARE_FILE (fu_efi_firmware_file_get_type())
G_DECLARE_DERIVABLE_TYPE(FuEfiFirmwareFile, fu_efi_firmware_file, FU, EFI_FIRMWARE_FILE, FuFirmware)

struct _FuEfiFirmwareFileClass {
	FuFirmwareClass parent_class;
};

FuFirmware *
fu_efi_firmware_file_new(void);
