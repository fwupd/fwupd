/*
 * Copyright 2026 Yuchao Li <liyc44@lenovo.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_LENOVO_ACCESSORY_HID_CHILD_DEVICE (fu_lenovo_accessory_hid_child_device_get_type())
G_DECLARE_FINAL_TYPE(FuLenovoAccessoryHidChildDevice,
		     fu_lenovo_accessory_hid_child_device,
		     FU,
		     LENOVO_ACCESSORY_HID_CHILD_DEVICE,
		     FuDevice)

FuLenovoAccessoryHidChildDevice *
fu_lenovo_accessory_hid_child_device_new(FuDevice *proxy);

guint16
fu_lenovo_accessory_hid_child_device_get_pid(FuLenovoAccessoryHidChildDevice *self);

void
fu_lenovo_accessory_hid_child_device_set_pid(FuLenovoAccessoryHidChildDevice *self, guint16 pid);

void
fu_lenovo_accessory_hid_child_device_set_target_slot(FuLenovoAccessoryHidChildDevice *self,
						     guint8 target_slot);
