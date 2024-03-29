/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_TEST_BLE_DEVICE (fu_test_ble_device_get_type())
G_DECLARE_FINAL_TYPE(FuTestBleDevice, fu_test_ble_device, FU, TEST_BLE_DEVICE, FuBluezDevice)
