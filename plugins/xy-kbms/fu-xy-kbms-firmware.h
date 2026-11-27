/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_XY_KBMS_FIRMWARE (fu_xy_kbms_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuXyKbmsFirmware, fu_xy_kbms_firmware, FU, XY_KBMS_FIRMWARE, FuFirmware)

FuFirmware *
fu_xy_kbms_firmware_new(void);
