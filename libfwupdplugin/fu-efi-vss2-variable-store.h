/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-firmware.h"

#define FU_TYPE_EFI_VSS2_VARIABLE_STORE (fu_efi_vss2_variable_store_get_type())
G_DECLARE_FINAL_TYPE(FuEfiVss2VariableStore,
		     fu_efi_vss2_variable_store,
		     FU,
		     EFI_VSS2_VARIABLE_STORE,
		     FuFirmware)

FuFirmware *
fu_efi_vss2_variable_store_new(void);
