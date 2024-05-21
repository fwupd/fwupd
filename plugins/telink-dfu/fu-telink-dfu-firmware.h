/*
 * Copyright 2024 Mike Chang <Mike.chang@telink-semi.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_TELINK_DFU_FIRMWARE (fu_telink_dfu_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuTelinkDfuFirmware,
		     fu_telink_dfu_firmware,
		     FU,
		     TELINK_DFU_FIRMWARE,
		     FuFirmware)

FuFirmware *
fu_telink_dfu_firmware_new(void);
guint32
fu_telink_dfu_firmware_get_crc32(FuTelinkDfuFirmware *self);
