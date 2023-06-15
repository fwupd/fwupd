/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-firmware.h"

#define FU_TYPE_EFI_DEVICE_PATH (fu_efi_device_path_get_type())
G_DECLARE_DERIVABLE_TYPE(FuEfiDevicePath, fu_efi_device_path, FU, EFI_DEVICE_PATH, FuFirmware)

struct _FuEfiDevicePathClass {
	FuFirmwareClass parent_class;
};

FuEfiDevicePath *
fu_efi_device_path_new(void);
guint8
fu_efi_device_path_get_subtype(FuEfiDevicePath *self);
void
fu_efi_device_path_set_subtype(FuEfiDevicePath *self, guint8 subtype);
