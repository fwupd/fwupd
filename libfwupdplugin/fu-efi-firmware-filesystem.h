/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-firmware.h"

#define FU_TYPE_EFI_FIRMWARE_FILESYSTEM (fu_efi_firmware_filesystem_get_type())
G_DECLARE_DERIVABLE_TYPE(FuEfiFirmwareFilesystem,
			 fu_efi_firmware_filesystem,
			 FU,
			 EFI_FIRMWARE_FILESYSTEM,
			 FuFirmware)

struct _FuEfiFirmwareFilesystemClass {
	FuFirmwareClass parent_class;
};

FuFirmware *
fu_efi_firmware_filesystem_new(void);
