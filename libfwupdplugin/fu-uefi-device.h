/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-device.h"

#define FU_TYPE_UEFI_DEVICE (fu_uefi_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuUefiDevice, fu_uefi_device, FU, UEFI_DEVICE, FuDevice)

struct _FuUefiDeviceClass {
	FuDeviceClass parent_class;
};

gboolean
fu_uefi_device_set_efivar_bytes(FuUefiDevice *self,
				const gchar *guid,
				const gchar *name,
				GBytes *bytes,
				guint32 attr,
				GError **error) G_GNUC_NON_NULL(1, 2, 3, 4);
GBytes *
fu_uefi_device_get_efivar_bytes(FuUefiDevice *self,
				const gchar *guid,
				const gchar *name,
				guint32 *attr,
				GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 2, 3);
