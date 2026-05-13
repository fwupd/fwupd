/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_UEFI_SBAT_DEVICE (fu_uefi_sbat_device_get_type())
G_DECLARE_FINAL_TYPE(FuUefiSbatDevice, fu_uefi_sbat_device, FU, UEFI_SBAT_DEVICE, FuDevice)

FuUefiSbatDevice *
fu_uefi_sbat_device_new(FuContext *ctx, GBytes *blob, GError **error);
