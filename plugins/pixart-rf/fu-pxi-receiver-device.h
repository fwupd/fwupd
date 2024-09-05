/*
 * Copyright 2020 Jimmy Yu <Jimmy_yu@pixart.com>
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-pxi-common.h"

#define FU_TYPE_PXI_RECEIVER_DEVICE (fu_pxi_receiver_device_get_type())

G_DECLARE_FINAL_TYPE(FuPxiReceiverDevice,
		     fu_pxi_receiver_device,
		     FU,
		     PXI_RECEIVER_DEVICE,
		     FuHidrawDevice)
