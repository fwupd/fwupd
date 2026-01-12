/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-firmware.h"

#define FU_TYPE_EFI_FTW_STORE (fu_efi_ftw_store_get_type())
G_DECLARE_FINAL_TYPE(FuEfiFtwStore, fu_efi_ftw_store, FU, EFI_FTW_STORE, FuFirmware)
