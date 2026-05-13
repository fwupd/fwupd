/*
 * Copyright 2020 Jimmy Yu <Jimmy_yu@pixart.com>
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-pixart-rf-common.h"

#define FU_TYPE_PIXART_RF_WIRELESS_DEVICE (fu_pixart_rf_wireless_device_get_type())

G_DECLARE_FINAL_TYPE(FuPixartRfWirelessDevice,
		     fu_pixart_rf_wireless_device,
		     FU,
		     PIXART_RF_WIRELESS_DEVICE,
		     FuDevice)

FuPixartRfWirelessDevice *
fu_pixart_rf_wireless_device_new(FuDevice *proxy, FuPixartRfOtaFwDevModel *model);
