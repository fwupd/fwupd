/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

// This -> FuHidrawDevice -> FuUdevDevice -> FuDevice

#define FU_TYPE_SUNWINON_BLE_HID_DEVICE (fu_sunwinon_ble_hid_device_get_type())
G_DECLARE_FINAL_TYPE(FuSunwinonBleHidDevice,
		     fu_sunwinon_ble_hid_device,
		     FU,
		     SUNWINON_BLE_HID_DEVICE,
		     FuHidrawDevice)
