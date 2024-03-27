/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-firmware.h"

#define FU_TYPE_EFI_FILESYSTEM (fu_efi_filesystem_get_type())
G_DECLARE_DERIVABLE_TYPE(FuEfiFilesystem, fu_efi_filesystem, FU, EFI_FILESYSTEM, FuFirmware)

struct _FuEfiFilesystemClass {
	FuFirmwareClass parent_class;
};

FuFirmware *
fu_efi_filesystem_new(void);
