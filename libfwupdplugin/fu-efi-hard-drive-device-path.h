/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
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
fu_efi_hard_drive_device_path_new_from_volume(FuVolume *volume, GError **error) G_GNUC_NON_NULL(1);

gboolean
fu_efi_hard_drive_device_path_compare(FuEfiHardDriveDevicePath *dp1, FuEfiHardDriveDevicePath *dp2)
    G_GNUC_NON_NULL(1, 2);
const fwupd_guid_t *
fu_efi_hard_drive_device_path_get_partition_signature(FuEfiHardDriveDevicePath *self)
    G_GNUC_NON_NULL(1);
guint64
fu_efi_hard_drive_device_path_get_partition_size(FuEfiHardDriveDevicePath *self) G_GNUC_NON_NULL(1);
guint64
fu_efi_hard_drive_device_path_get_partition_start(FuEfiHardDriveDevicePath *self)
    G_GNUC_NON_NULL(1);
guint32
fu_efi_hard_drive_device_path_get_partition_number(FuEfiHardDriveDevicePath *self)
    G_GNUC_NON_NULL(1);
