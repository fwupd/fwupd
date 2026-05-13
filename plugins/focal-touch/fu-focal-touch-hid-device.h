/*
 * Copyright 2025 Shihwei Huang <shihwei.huang@focaltech-electronics.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_FOCAL_TOUCH_HID_DEVICE (fu_focal_touch_hid_device_get_type())
G_DECLARE_FINAL_TYPE(FuFocalTouchHidDevice,
		     fu_focal_touch_hid_device,
		     FU,
		     FOCAL_TOUCH_HID_DEVICE,
		     FuHidrawDevice)
