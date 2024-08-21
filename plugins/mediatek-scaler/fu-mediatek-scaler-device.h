/*
 * Copyright 2023 Dell Technologies
 * Copyright 2023 Mediatek Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_MEDIATEK_SCALER_DEVICE (fu_mediatek_scaler_device_get_type())
G_DECLARE_FINAL_TYPE(FuMediatekScalerDevice,
		     fu_mediatek_scaler_device,
		     FU,
		     MEDIATEK_SCALER_DEVICE,
		     FuDrmDevice)
