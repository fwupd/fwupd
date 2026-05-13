/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_MTD_DEVICE (fu_mtd_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuMtdDevice, fu_mtd_device, FU, MTD_DEVICE, FuUdevDevice)

struct _FuMtdDeviceClass {
	FuUdevDeviceClass parent_class;
};

#define FU_MTD_DEVICE_FLAG_SMBIOS_VERSION_FALLBACK "smbios-version-fallback"

gboolean
fu_mtd_device_write_image(FuMtdDevice *self, FuFirmware *img, FuProgress *progress, GError **error)
    G_GNUC_NON_NULL(1, 2, 3);
