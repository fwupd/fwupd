/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-firmware.h"

#define FU_TYPE_SOLOKEY_FIRMWARE (fu_solokey_firmware_get_type ())
G_DECLARE_FINAL_TYPE (FuSolokeyFirmware, fu_solokey_firmware, FU, SOLOKEY_FIRMWARE, FuFirmware)

FuFirmware		*fu_solokey_firmware_new	(void);
