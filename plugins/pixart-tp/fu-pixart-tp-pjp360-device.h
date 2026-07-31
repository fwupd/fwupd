/*
 * Copyright 2025 Harris Tai <harris_tai@pixart.com>
 * Copyright 2025 Micky Hsieh <micky_hsieh@pixart.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-pixart-tp-device.h"

#define FU_TYPE_PIXART_TP_PJP360_DEVICE (fu_pixart_tp_pjp360_device_get_type())
G_DECLARE_FINAL_TYPE(FuPixartTpPjp360Device,
		     fu_pixart_tp_pjp360_device,
		     FU,
		     PIXART_TP_PJP360_DEVICE,
		     FuPixartTpDevice)
