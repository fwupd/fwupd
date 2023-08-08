/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-firmware.h"

#define FU_TYPE_EFI_LOAD_OPTION (fu_efi_load_option_get_type())

G_DECLARE_FINAL_TYPE(FuEfiLoadOption, fu_efi_load_option, FU, EFI_LOAD_OPTION, FuFirmware)

GBytes *
fu_efi_load_option_get_optional_data(FuEfiLoadOption *self);
void
fu_efi_load_option_set_optional_data(FuEfiLoadOption *self, GBytes *optional_data);
gboolean
fu_efi_load_option_set_optional_path(FuEfiLoadOption *self,
				     const gchar *optional_path,
				     GError **error);

FuEfiLoadOption *
fu_efi_load_option_new(void) G_GNUC_WARN_UNUSED_RESULT;
FuEfiLoadOption *
fu_efi_load_option_new_esp_for_boot_entry(guint16 boot_entry,
					  GError **error) G_GNUC_WARN_UNUSED_RESULT;
