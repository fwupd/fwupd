/*
 * Copyright 2026 SHENZHEN Betterlife Electronic CO.LTD <xiaomaoting@blestech.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_BLESTECH_TP_FIRMWARE (fu_blestech_tp_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuBlestechTpFirmware,
		     fu_blestech_tp_firmware,
		     FU,
		     BLESTECH_TP_FIRMWARE,
		     FuFirmware)

guint16
fu_blestech_tp_firmware_get_checksum(FuBlestechTpFirmware *self);
