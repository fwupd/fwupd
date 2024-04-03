/*
 * Copyright 2024 Randy Lai <randy.lai@weidahitech.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_WEIDA_RAW_FIRMWARE (fu_weida_raw_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuWeidaRawFirmware, fu_weida_raw_firmware, FU, WEIDA_RAW_FIRMWARE, FuFirmware)

FuFirmware *
fu_weida_raw_firmware_new(void);
guint16
fu_weida_raw_firmware_get_start_addr(FuWeidaRawFirmware *self);
