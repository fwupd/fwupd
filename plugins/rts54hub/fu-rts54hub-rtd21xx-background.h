/*
 * Copyright (C) 2021 Ricky Wu <ricky_wu@realtek.com> <spring1527@gmail.com>
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-rts54hub-rtd21xx-device.h"

#define FU_TYPE_RTS54HUB_RTD21XX_BACKGROUND (fu_rts54hub_rtd21xx_background_get_type ())
G_DECLARE_FINAL_TYPE (FuRts54hubRtd21xxBackground, fu_rts54hub_rtd21xx_background, FU, RTS54HUB_RTD21XX_BACKGROUND, FuRts54hubRtd21xxDevice)
