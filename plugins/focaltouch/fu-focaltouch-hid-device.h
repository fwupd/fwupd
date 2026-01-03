/*
 * Copyright 2025 Shihwei Huang <shihwei.huang@focaltech-electronics.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_FOCALTOUCH_HID_DEVICE (fu_focaltouch_hid_device_get_type())
G_DECLARE_FINAL_TYPE(FuFocaltouchHidDevice,
		     fu_focaltouch_hid_device,
		     FU,
		     FOCALTOUCH_HID_DEVICE,
		     FuHidrawDevice)
