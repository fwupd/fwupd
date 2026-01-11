/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_SUNWINON_HID_DEVICE (fu_sunwinon_hid_device_get_type())
G_DECLARE_FINAL_TYPE(FuSunwinonHidDevice,
		     fu_sunwinon_hid_device,
		     FU,
		     SUNWINON_HID_DEVICE,
		     FuHidrawDevice)
