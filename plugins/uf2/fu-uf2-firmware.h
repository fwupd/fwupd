/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_UF2_FIRMWARE (fu_uf2_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuUf2Firmware, fu_uf2_firmware, FU, UF2_FIRMWARE, FuFirmware)

FuFirmware *
fu_uf2_firmware_new(void);
