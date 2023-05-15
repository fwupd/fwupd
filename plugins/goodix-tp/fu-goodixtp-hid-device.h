/*
 * Copyright (C) 2023 Goodix.inc <xulinkun@goodix.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-goodixtp-common.h"

#define FU_TYPE_GOODIXTP_HID_DEVICE (fu_goodixtp_hid_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuGoodixtpHidDevice,
			 fu_goodixtp_hid_device,
			 FU,
			 GOODIXTP_HID_DEVICE,
			 FuUdevDevice)

struct _FuGoodixtpHidDeviceClass {
	FuUdevDeviceClass parent_class;
};

gboolean
fu_goodixtp_hid_device_get_report(FuDevice *device, guint8 *buf, GError **error);
gboolean
fu_goodixtp_hid_device_set_report(FuDevice *device, guint8 *buf, guint32 len, GError **error);
void
fu_goodixtp_hid_device_set_version(FuGoodixtpHidDevice *self, struct FuGoodixVersion *version);
guint8
fu_goodixtp_hid_device_get_sensor_id(FuGoodixtpHidDevice *self);
