/*
 *  Copyright (C) 2022 Jingle Wu <jingle.wu@emc.com.tw>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_ELANHAPTIC_FIRMWARE (fu_elanhaptic_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuElanhapticFirmware,
		     fu_elanhaptic_firmware,
		     FU,
		     ELANHAPTIC_FIRMWARE,
		     FuFirmware)

FuFirmware *
fu_elanhaptic_firmware_new(void);

guint16
fu_elanhaptic_firmware_get_driveric(FuElanhapticFirmware *self);
