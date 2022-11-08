/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+ OR MIT
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_TI_TPS6598X_PD_DEVICE (fu_ti_tps6598x_pd_device_get_type())
G_DECLARE_FINAL_TYPE(FuTiTps6598xPdDevice,
		     fu_ti_tps6598x_pd_device,
		     FU,
		     TI_TPS6598X_PD_DEVICE,
		     FuDevice)

FuDevice *
fu_ti_tps6598x_pd_device_new(FuContext *ctx, guint8 target);
