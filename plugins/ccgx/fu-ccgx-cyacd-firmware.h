/*
 * Copyright (C) 2020 Cypress Semiconductor Corporation.
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-firmware.h"

#define FU_TYPE_CCGX_CYACD_FIRMWARE (fu_ccgx_cyacd_firmware_get_type ())
G_DECLARE_FINAL_TYPE (FuCcgxCyacdFirmware, fu_ccgx_cyacd_firmware, FU,CCGX_CYACD_FIRMWARE, FuFirmware)

FuFirmware	*fu_ccgx_cyacd_firmware_new		(void);
