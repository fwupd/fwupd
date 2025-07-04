/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-mm-device.h"

#define FU_TYPE_MM_FASTBOOT_DEVICE (fu_mm_fastboot_device_get_type())
G_DECLARE_FINAL_TYPE(FuMmFastbootDevice, fu_mm_fastboot_device, FU, MM_FASTBOOT_DEVICE, FuMmDevice)

void
fu_mm_fastboot_device_set_detach_at(FuMmFastbootDevice *self, const gchar *detach_at)
    G_GNUC_NON_NULL(1, 2);
