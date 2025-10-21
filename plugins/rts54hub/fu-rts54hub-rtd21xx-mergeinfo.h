/*
 * Copyright 2025 Realtek Corporation
 * Copyright 2025 Shadow Zhang <shadow_zhang@realsil.com.cn>
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-rts54hub-rtd21xx-device.h"

#define FU_TYPE_RTS54HUB_RTD21XX_MERGEINFO (fu_rts54hub_rtd21xx_mergeinfo_get_type())
G_DECLARE_FINAL_TYPE(FuRts54hubRtd21xxMergeinfo,
		     fu_rts54hub_rtd21xx_mergeinfo,
		     FU,
		     RTS54HUB_RTD21XX_MERGEINFO,
		     FuRts54hubRtd21xxDevice)
