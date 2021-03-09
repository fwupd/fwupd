/*
 * Copyright (C) 2020 Jimmy Yu <Jimmy_yu@pixart.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-firmware.h"

#define FU_PXI_DEVICE_MODEL_NAME_LEN		12	/* bytes */

#define FU_TYPE_PXI_FIRMWARE (fu_pxi_firmware_get_type ())
G_DECLARE_FINAL_TYPE (FuPxiFirmware, fu_pxi_firmware, FU, PXI_FIRMWARE, FuFirmware)

FuFirmware		*fu_pxi_firmware_new		(void);
const gchar		*fu_pxi_firmware_get_model_name	(FuPxiFirmware	*self);
