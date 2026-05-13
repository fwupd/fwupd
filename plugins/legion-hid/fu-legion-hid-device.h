/*
 * Copyright 2025 lazro <2059899519@qq.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-legion-hid-struct.h"

#define FU_TYPE_LEGION_HID_DEVICE (fu_legion_hid_device_get_type())
G_DECLARE_FINAL_TYPE(FuLegionHidDevice, fu_legion_hid_device, FU, LEGION_HID_DEVICE, FuHidrawDevice)

gboolean
fu_legion_hid_device_execute_upgrade(FuLegionHidDevice *self, FuFirmware *firmware, GError **error);
gboolean
fu_legion_hid_device_get_version(FuLegionHidDevice *self,
				 FuLegionHidDeviceId id,
				 guint32 *version,
				 GError **error);
