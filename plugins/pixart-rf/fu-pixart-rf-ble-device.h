/*
 * Copyright 2020 Jimmy Yu <Jimmy_yu@pixart.com>
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_PIXART_RF_BLE_DEVICE (fu_pixart_rf_ble_device_get_type())

G_DECLARE_FINAL_TYPE(FuPixartRfBleDevice,
		     fu_pixart_rf_ble_device,
		     FU,
		     PIXART_RF_BLE_DEVICE,
		     FuHidrawDevice)
