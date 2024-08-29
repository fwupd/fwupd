/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-udev-device.h"

#define FU_TYPE_HIDRAW_DEVICE (fu_hidraw_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuHidrawDevice, fu_hidraw_device, FU, HIDRAW_DEVICE, FuUdevDevice)

struct _FuHidrawDeviceClass {
	FuUdevDeviceClass parent_class;
};
