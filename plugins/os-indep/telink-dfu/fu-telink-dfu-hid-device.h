/*
 * Copyright 2024 Mike Chang <Mike.chang@telink-semi.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_TELINK_DFU_HID_DEVICE (fu_telink_dfu_hid_device_get_type())
G_DECLARE_FINAL_TYPE(FuTelinkDfuHidDevice,
		     fu_telink_dfu_hid_device,
		     FU,
		     TELINK_DFU_HID_DEVICE,
		     FuHidDevice)
