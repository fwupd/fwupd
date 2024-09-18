/*
 * Copyright 2024 Mike Chang <Mike.chang@telink-semi.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_TELINK_DFU_BLE_DEVICE (fu_telink_dfu_ble_device_get_type())
G_DECLARE_FINAL_TYPE(FuTelinkDfuBleDevice,
		     fu_telink_dfu_ble_device,
		     FU,
		     TELINK_DFU_BLE_DEVICE,
		     FuBluezDevice)
