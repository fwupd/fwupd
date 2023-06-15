/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-efi-device-path.h"

#define FU_TYPE_EFI_FILE_PATH_DEVICE_PATH (fu_efi_file_path_device_path_get_type())
G_DECLARE_FINAL_TYPE(FuEfiFilePathDevicePath,
		     fu_efi_file_path_device_path,
		     FU,
		     EFI_FILE_PATH_DEVICE_PATH,
		     FuEfiDevicePath)

FuEfiFilePathDevicePath *
fu_efi_file_path_device_path_new(void);

gchar *
fu_efi_file_path_device_path_get_name(FuEfiFilePathDevicePath *self, GError **error);
gboolean
fu_efi_file_path_device_path_set_name(FuEfiFilePathDevicePath *self,
				      const gchar *name,
				      GError **error);
