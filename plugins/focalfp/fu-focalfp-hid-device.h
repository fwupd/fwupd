/*
 * Copyright (C) 2022 Shihwei Huang <shihwei.huang@focaltech-electronics.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_FOCALFP_HID_DEVICE (fu_focalfp_hid_device_get_type())
G_DECLARE_FINAL_TYPE(FuFocalfpHidDevice,
		     fu_focalfp_hid_device,
		     FU,
		     FOCALFP_HID_DEVICE,
		     FuUdevDevice)
