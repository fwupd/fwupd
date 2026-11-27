/*
 * Copyright 2021 Ricky Wu <ricky_wu@realtek.com> <spring1527@gmail.com>
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-realtek-rts54hub-rtd21xx-device.h"

#define FU_TYPE_REALTEK_RTS54HUB_RTD21XX_BACKGROUND                                                \
	(fu_realtek_rts54hub_rtd21xx_background_get_type())
G_DECLARE_FINAL_TYPE(FuRealtekRts54hubRtd21xxBackground,
		     fu_realtek_rts54hub_rtd21xx_background,
		     FU,
		     REALTEK_RTS54HUB_RTD21XX_BACKGROUND,
		     FuRealtekRts54hubRtd21xxDevice)
