/*
 * Copyright 2026 SHENZHEN Betterlife Electronic CO.LTD <xiaomaoting@blestech.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_BLESTECHTP_FIRMWARE (fu_blestechtp_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuBlestechtpFirmware, fu_blestechtp_firmware, FU, BLESTECHTP_FIRMWARE, FuFirmware)

guint16
fu_blestechtp_firmware_get_checksum(FuBlestechtpFirmware *self);