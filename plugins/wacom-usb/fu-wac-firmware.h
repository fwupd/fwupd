/*
 * Copyright (C) 2018-2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-firmware.h"

#define FU_TYPE_WAC_FIRMWARE (fu_wac_firmware_get_type ())
G_DECLARE_FINAL_TYPE (FuWacFirmware, fu_wac_firmware, FU, WAC_FIRMWARE, FuFirmware)

FuFirmware		*fu_wac_firmware_new	(void);
