/*
 * Copyright 2020 Jimmy Yu <Jimmy_yu@pixart.com>
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-pixart-rf-common.h"

#define FU_TYPE_PIXART_RF_RECEIVER_DEVICE (fu_pixart_rf_receiver_device_get_type())

G_DECLARE_FINAL_TYPE(FuPixartRfReceiverDevice,
		     fu_pixart_rf_receiver_device,
		     FU,
		     PIXART_RF_RECEIVER_DEVICE,
		     FuHidrawDevice)
