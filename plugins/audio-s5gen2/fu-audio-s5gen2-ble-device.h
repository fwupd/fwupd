/*
 * Copyright 2024 Denis Pynkin <denis.pynkin@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_QC_S5GEN2_BLE_DEVICE (fu_qc_s5gen2_ble_device_get_type())
G_DECLARE_FINAL_TYPE(FuQcS5gen2BleDevice,
		     fu_qc_s5gen2_ble_device,
		     FU,
		     QC_S5GEN2_BLE_DEVICE,
		     FuBluezDevice)
