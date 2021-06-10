/*
 * Copyright (C) 2020 Jimmy Yu <Jimmy_yu@pixart.com>
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-plugin.h"
#include "fu-pxi-common.h"

#define FU_TYPE_PXI_DONGLE_RECEIVER (fu_pxi_dongle_receiver_get_type ())

G_DECLARE_FINAL_TYPE (FuPxiDongleReceiver, fu_pxi_dongle_receiver, FU, PXI_DONGLE_RECEIVER, FuUdevDevice)

