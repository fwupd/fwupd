/*
 * Copyright (C) 2021 Gaël PORTAY <gael.portay@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-genesys-common.h"

#define FU_TYPE_GENESYS_SCALER_FIRMWARE (fu_genesys_scaler_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuGenesysScalerFirmware,
		     fu_genesys_scaler_firmware,
		     FU,
		     GENESYS_SCALER_FIRMWARE,
		     FuFirmware)

#define GENESYS_SCALER_BANK_SIZE 0x200000U

FuFirmware *
fu_genesys_scaler_firmware_new(void);
