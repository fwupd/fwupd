/*
 * Copyright 2023 Dell Technologies
 * Copyright 2023 Mediatek Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_MEDIATEK_SCALER_FIRMWARE (fu_mediatek_scaler_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuMediatekScalerFirmware,
		     fu_mediatek_scaler_firmware,
		     FU,
		     MEDIATEK_SCALER_FIRMWARE,
		     FuFirmware)

FuFirmware *
fu_mediatek_scaler_firmware_new(void);
