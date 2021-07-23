/*
 * Copyright (C) 2020 Jimmy Yu <Jimmy_yu@pixart.com>
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-pxi-common.h"

#define FU_TYPE_PXI_WIRELESS_DEVICE (fu_pxi_wireless_device_get_type ())

G_DECLARE_FINAL_TYPE (FuPxiWirelessDevice, fu_pxi_wireless_device, FU, PXI_WIRELESS_DEVICE, FuDevice)

FuPxiWirelessDevice *fu_pxi_wireless_device_new	(struct ota_fw_dev_model *model);
