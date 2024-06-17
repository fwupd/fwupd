/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_WEIDA_RAW_FIRMWARE (fu_weida_raw_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuWeidaRawFirmware, fu_weida_raw_firmware, FU, WEIDA_RAW_FIRMWARE, FuFirmware)
