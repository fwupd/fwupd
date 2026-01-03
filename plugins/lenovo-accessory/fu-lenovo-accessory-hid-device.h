/*
 * Copyright 2024 Mario Limonciello <superm1@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_LENOVO_ACCESSORY_HID_DEVICE (fu_lenovo_accessory_hid_device_get_type())
G_DECLARE_FINAL_TYPE(FuLenovoAccessoryHidDevice,
		     fu_lenovo_accessory_hid_device,
		     FU,
		     LENOVO_HID_DEVICE,
		     FuHidrawDevice)
