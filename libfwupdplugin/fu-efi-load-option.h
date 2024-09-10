/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-firmware.h"

#define FU_TYPE_EFI_LOAD_OPTION (fu_efi_load_option_get_type())

G_DECLARE_FINAL_TYPE(FuEfiLoadOption, fu_efi_load_option, FU, EFI_LOAD_OPTION, FuFirmware)

gchar *
fu_efi_load_option_get_optional_path(FuEfiLoadOption *self, GError **error) G_GNUC_NON_NULL(1);
gboolean
fu_efi_load_option_set_optional_path(FuEfiLoadOption *self,
				     const gchar *optional_path,
				     GError **error) G_GNUC_NON_NULL(1);

FuEfiLoadOption *
fu_efi_load_option_new(void) G_GNUC_WARN_UNUSED_RESULT;
