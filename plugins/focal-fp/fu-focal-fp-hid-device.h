/*
 * Copyright 2022 Shihwei Huang <shihwei.huang@focaltech-electronics.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_FOCAL_FP_HID_DEVICE (fu_focal_fp_hid_device_get_type())
G_DECLARE_FINAL_TYPE(FuFocalFpHidDevice,
		     fu_focal_fp_hid_device,
		     FU,
		     FOCAL_FP_HID_DEVICE,
		     FuHidrawDevice)
