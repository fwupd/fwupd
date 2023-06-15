/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-efi-device-path.h"
#include "fu-firmware.h"

#define FU_TYPE_EFI_DEVICE_PATH_LIST (fu_efi_device_path_list_get_type())

G_DECLARE_FINAL_TYPE(FuEfiDevicePathList,
		     fu_efi_device_path_list,
		     FU,
		     EFI_DEVICE_PATH_LIST,
		     FuFirmware)

FuEfiDevicePathList *
fu_efi_device_path_list_new(void) G_GNUC_WARN_UNUSED_RESULT;
