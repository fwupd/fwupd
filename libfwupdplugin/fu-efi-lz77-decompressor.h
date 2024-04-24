/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-efi-struct.h"
#include "fu-firmware.h"

#define FU_TYPE_EFI_LZ77_DECOMPRESSOR (fu_efi_lz77_decompressor_get_type())
G_DECLARE_FINAL_TYPE(FuEfiLz77Decompressor,
		     fu_efi_lz77_decompressor,
		     FU,
		     EFI_LZ77_DECOMPRESSOR,
		     FuFirmware)

FuFirmware *
fu_efi_lz77_decompressor_new(void);
