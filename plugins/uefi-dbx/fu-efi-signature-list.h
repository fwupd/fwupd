/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-firmware.h"

#define FU_TYPE_EFI_SIGNATURE_LIST (fu_efi_signature_list_get_type ())
G_DECLARE_FINAL_TYPE (FuEfiSignatureList, fu_efi_signature_list, FU, EFI_SIGNATURE_LIST, FuFirmware)

FuFirmware		*fu_efi_signature_list_new		(void);
