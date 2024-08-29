/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-udev-device.h"

#define FU_TYPE_SERIO_DEVICE (fu_serio_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuSerioDevice, fu_serio_device, FU, SERIO_DEVICE, FuUdevDevice)

struct _FuSerioDeviceClass {
	FuUdevDeviceClass parent_class;
};
