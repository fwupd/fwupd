/*
 * Copyright (C) 2021 Texas Instruments Incorporated
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+ OR MIT
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_TI_TPS6598X_FIRMWARE (fu_ti_tps6598x_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuTiTps6598xFirmware,
		     fu_ti_tps6598x_firmware,
		     FU,
		     TI_TPS6598X_FIRMWARE,
		     FuFirmware)

FuFirmware *
fu_ti_tps6598x_firmware_new(void);
