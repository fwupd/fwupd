/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_CFU_FIRMWARE (fu_cfu_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuCfuFirmware, fu_cfu_firmware, FU, CFU_FIRMWARE, FuFirmware)

FuFirmware *
fu_cfu_firmware_new(void);
