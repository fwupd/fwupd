/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-udev-device.h"

#define FU_TYPE_BLOCK_DEVICE (fu_block_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuBlockDevice, fu_block_device, FU, BLOCK_DEVICE, FuUdevDevice)

struct _FuBlockDeviceClass {
	FuUdevDeviceClass parent_class;
};
