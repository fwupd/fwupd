/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-efi-device-path.h"
#include "fu-volume.h"

#define FU_TYPE_EFI_HARD_DRIVE_DEVICE_PATH (fu_efi_hard_drive_device_path_get_type())
G_DECLARE_FINAL_TYPE(FuEfiHardDriveDevicePath,
		     fu_efi_hard_drive_device_path,
		     FU,
		     EFI_HARD_DRIVE_DEVICE_PATH,
		     FuEfiDevicePath)

FuEfiHardDriveDevicePath *
fu_efi_hard_drive_device_path_new(void);
FuEfiHardDriveDevicePath *
fu_efi_hard_drive_device_path_new_from_volume(FuVolume *volume, GError **error);
