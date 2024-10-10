/*
 * Copyright 2024 Mario Limonciello <superm1@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_ASUS_HID_DEVICE (fu_asus_hid_device_get_type())
G_DECLARE_FINAL_TYPE(FuAsusHidDevice, fu_asus_hid_device, FU, ASUS_HID_DEVICE, FuHidDevice)
