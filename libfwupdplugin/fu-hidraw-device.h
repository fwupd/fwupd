/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-hid-descriptor.h"
#include "fu-udev-device.h"

#define FU_TYPE_HIDRAW_DEVICE (fu_hidraw_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuHidrawDevice, fu_hidraw_device, FU, HIDRAW_DEVICE, FuUdevDevice)

struct _FuHidrawDeviceClass {
	FuUdevDeviceClass parent_class;
};

gboolean
fu_hidraw_device_set_feature(FuHidrawDevice *self,
			     const guint8 *buf,
			     gsize bufsz,
			     FuIoctlFlags flags,
			     GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 2);
gboolean
fu_hidraw_device_get_feature(FuHidrawDevice *self,
			     guint8 *buf,
			     gsize bufsz,
			     FuIoctlFlags flags,
			     GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 2);
FuHidDescriptor *
fu_hidraw_device_parse_descriptor(FuHidrawDevice *self, GError **error) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1);
