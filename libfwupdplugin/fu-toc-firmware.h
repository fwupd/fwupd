/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-firmware.h"

#define FU_TYPE_TOC_FIRMWARE (fu_toc_firmware_get_type ())
G_DECLARE_FINAL_TYPE (FuTocFirmware, fu_toc_firmware, FU, TOC_FIRMWARE, FuFirmware)

FuFirmware	*fu_toc_firmware_new		(void);
