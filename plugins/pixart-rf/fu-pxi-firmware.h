/*
 * Copyright (C) 2020 Jimmy Yu <Jimmy_yu@pixart.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-firmware.h"

#define FU_TYPE_PXI_RF_FIRMWARE (fu_pxi_rf_firmware_get_type ())
G_DECLARE_FINAL_TYPE (FuPxiRfFirmware, fu_pxi_rf_firmware, FU, PXI_RF_FIRMWARE, FuFirmware)

FuFirmware		*fu_pxi_rf_firmware_new		(void);
