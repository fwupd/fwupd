/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_FPC_FF2_FIRMWARE (fu_fpc_ff2_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuFpcFf2Firmware, fu_fpc_ff2_firmware, FU, FPC_FF2_FIRMWARE, FuFirmware)

guint32
fu_fpc_ff2_firmware_get_blocks_num(FuFpcFf2Firmware *self);
