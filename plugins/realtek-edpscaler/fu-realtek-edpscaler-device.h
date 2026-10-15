/*
 * Copyright 2026 Realtek Corporation
 * Copyright 2026 Justin Li <justin_li@realsil.com.cn>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_REALTEK_EDPSCALER_DEVICE (fu_realtek_edpscaler_device_get_type())
G_DECLARE_FINAL_TYPE(FuRealtekEdpscalerDevice,
		     fu_realtek_edpscaler_device,
		     FU,
		     REALTEK_EDPSCALER_DEVICE,
		     FuDrmDevice)
