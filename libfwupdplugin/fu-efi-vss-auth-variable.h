/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-firmware.h"

#define FU_TYPE_EFI_VSS_AUTH_VARIABLE (fu_efi_vss_auth_variable_get_type())
G_DECLARE_FINAL_TYPE(FuEfiVssAuthVariable,
		     fu_efi_vss_auth_variable,
		     FU,
		     EFI_VSS_AUTH_VARIABLE,
		     FuFirmware)

FuEfiVariableState
fu_efi_vss_auth_variable_get_state(FuEfiVssAuthVariable *self) G_GNUC_NON_NULL(1);

FuFirmware *
fu_efi_vss_auth_variable_new(void);
