/*
 * Copyright 2025 Realtek Corporation
 * Copyright 2025 Shadow Zhang <shadow_zhang@realsil.com.cn>
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-realtek-rts54hub-rtd21xx-device.h"

#define FU_TYPE_REALTEK_RTS54HUB_RTD21XX_MERGEINFO                                                 \
	(fu_realtek_rts54hub_rtd21xx_mergeinfo_get_type())
G_DECLARE_FINAL_TYPE(FuRealtekRts54hubRtd21xxMergeinfo,
		     fu_realtek_rts54hub_rtd21xx_mergeinfo,
		     FU,
		     REALTEK_RTS54HUB_RTD21XX_MERGEINFO,
		     FuRealtekRts54hubRtd21xxDevice)
