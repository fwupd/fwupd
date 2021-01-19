/*
 * Copyright (C) 2021 Ricky Wu <ricky_wu@realtek.com> <spring1527@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-plugin.h"

#define FU_TYPE_RTS54HUB_RTD21XX_DEVICE (fu_rts54hub_rtd21xx_device_get_type ())
G_DECLARE_FINAL_TYPE (FuRts54hubRtd21xxDevice, fu_rts54hub_rtd21xx_device, FU, RTS54HUB_RTD21XX_DEVICE, FuDevice)

FuRts54hubRtd21xxDevice		*fu_rts54hub_rtd21xx_device_new	(void);
