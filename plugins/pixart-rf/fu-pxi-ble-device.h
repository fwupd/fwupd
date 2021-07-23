/*
 * Copyright (C) 2020 Jimmy Yu <Jimmy_yu@pixart.com>
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_PXI_BLE_DEVICE (fu_pxi_ble_device_get_type ())

G_DECLARE_FINAL_TYPE (FuPxiBleDevice, fu_pxi_ble_device, FU, PXI_BLE_DEVICE, FuUdevDevice)
