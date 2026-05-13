/*
 * Copyright 2026 Yuchao Li <liyc44@lenovo.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_LENOVO_ACCESSORY_BLE_DEVICE (fu_lenovo_accessory_ble_device_get_type())
G_DECLARE_FINAL_TYPE(FuLenovoAccessoryBleDevice,
		     fu_lenovo_accessory_ble_device,
		     FU,
		     LENOVO_ACCESSORY_BLE_DEVICE,
		     FuBluezDevice)
