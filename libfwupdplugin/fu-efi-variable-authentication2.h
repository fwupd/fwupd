/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-efi-signature-list.h"

#define FU_TYPE_EFI_VARIABLE_AUTHENTICATION2 (fu_efi_variable_authentication2_get_type())
G_DECLARE_FINAL_TYPE(FuEfiVariableAuthentication2,
		     fu_efi_variable_authentication2,
		     FU,
		     EFI_VARIABLE_AUTHENTICATION2,
		     FuEfiSignatureList)

GPtrArray *
fu_efi_variable_authentication2_get_signers(FuEfiVariableAuthentication2 *self) G_GNUC_NON_NULL(1);
