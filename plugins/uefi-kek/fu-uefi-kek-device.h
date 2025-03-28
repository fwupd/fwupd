/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_UEFI_KEK_DEVICE (fu_uefi_kek_device_get_type())
G_DECLARE_FINAL_TYPE(FuUefiKekDevice, fu_uefi_kek_device, FU, UEFI_KEK_DEVICE, FuUefiDevice)
