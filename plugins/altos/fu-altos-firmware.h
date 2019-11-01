/*
 * Copyright (C) 2017-2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-firmware.h"

#define FU_TYPE_ALTOS_FIRMWARE (fu_altos_firmware_get_type ())

G_DECLARE_FINAL_TYPE (FuAltosFirmware, fu_altos_firmware, FU, ALTOS_FIRMWARE, FuFirmware)

FuFirmware		*fu_altos_firmware_new		(void);
