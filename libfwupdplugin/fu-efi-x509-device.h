/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-device.h"
#include "fu-efi-x509-signature.h"

#define FU_TYPE_EFI_X509_DEVICE (fu_efi_x509_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuEfiX509Device, fu_efi_x509_device, FU, EFI_X509_DEVICE, FuDevice)

struct _FuEfiX509DeviceClass {
	FuDeviceClass parent_class;
};

FuEfiX509Device *
fu_efi_x509_device_new(FuContext *ctx, FuEfiX509Signature *sig) G_GNUC_NON_NULL(1, 2);
