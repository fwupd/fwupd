/*
 * Copyright (C) 2016-2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-firmware.h"

#define FU_TYPE_EBITDO_FIRMWARE (fu_ebitdo_firmware_get_type ())
G_DECLARE_FINAL_TYPE (FuEbitdoFirmware, fu_ebitdo_firmware, FU, EBITDO_FIRMWARE, FuFirmware)

FuFirmware			*fu_ebitdo_firmware_new			(void);
