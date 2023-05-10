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
fu_goodixtp_hid_device_get_report(FuGoodixtpHidDevice *self,
				  guint8 *buf,
				  guint32 bufsz,
				  GError **error);
gboolean
fu_goodixtp_hid_device_set_report(FuGoodixtpHidDevice *self,
				  guint8 *buf,
				  guint32 len,
				  GError **error);
void
fu_goodixtp_hid_device_set_patch_pid(FuGoodixtpHidDevice *self, const gchar *patch_pid);
void
fu_goodixtp_hid_device_set_patch_vid(FuGoodixtpHidDevice *self, const gchar *patch_vid);
void
fu_goodixtp_hid_device_set_sensor_id(FuGoodixtpHidDevice *self, guint8 sensor_id);
void
fu_goodixtp_hid_device_set_config_ver(FuGoodixtpHidDevice *self, guint8 ver);
guint8
fu_goodixtp_hid_device_get_sensor_id(FuGoodixtpHidDevice *self);
