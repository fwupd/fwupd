/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-udev-device.h"
#include "fu-v4l-struct.h"

#define FU_TYPE_V4L_DEVICE (fu_v4l_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuV4lDevice, fu_v4l_device, FU, V4L_DEVICE, FuUdevDevice)

struct _FuV4lDeviceClass {
	FuUdevDeviceClass parent_class;
};

guint8
fu_v4l_device_get_index(FuV4lDevice *self) G_GNUC_NON_NULL(1);
FuV4lCap
fu_v4l_device_get_caps(FuV4lDevice *self) G_GNUC_NON_NULL(1);
