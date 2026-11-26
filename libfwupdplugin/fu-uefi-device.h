/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-device.h"
#include "fu-efi-struct.h"

G_BEGIN_DECLS

#define FU_TYPE_UEFI_DEVICE (fu_uefi_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuUefiDevice, fu_uefi_device, FU, UEFI_DEVICE, FuDevice)

struct _FuUefiDeviceClass {
	FuDeviceClass parent_class;
};

/**
 * FU_UEFI_DEVICE_PRIVATE_FLAG_IS_EXTERNAL:
 *
 * The device is for a platform with an external out-of-band management agent.
 */
#define FU_UEFI_DEVICE_PRIVATE_FLAG_IS_EXTERNAL "is-external"

void
fu_uefi_device_set_kind(FuUefiDevice *self, FuEfiSecureBootSiglistKind kind) G_GNUC_NON_NULL(1);
FuEfiSecureBootSiglistKind
fu_uefi_device_get_kind(FuUefiDevice *self) G_GNUC_NON_NULL(1) G_GNUC_PURE;

G_END_DECLS
