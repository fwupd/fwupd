/*
 * Copyright (C) 2021 Gaël PORTAY <gael.portay@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_GENESYS_SCALER_DEVICE (fu_genesys_scaler_device_get_type())
G_DECLARE_FINAL_TYPE(FuGenesysScalerDevice,
		     fu_genesys_scaler_device,
		     FU,
		     GENESYS_SCALER_DEVICE,
		     FuDevice)

FuGenesysScalerDevice *
fu_genesys_scaler_device_new(FuContext *ctx);
