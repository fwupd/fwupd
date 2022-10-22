/*
 *  Copyright (C) 2022 Jingle Wu <jingle.wu@emc.com.tw>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_ELANTP_HAPTIC_FIRMWARE (fu_elantp_haptic_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuElantpHapticFirmware,
		     fu_elantp_haptic_firmware,
		     FU,
		     ELANTP_HAPTIC_FIRMWARE,
		     FuFirmware)

FuFirmware *
fu_elantp_haptic_firmware_new(void);

guint16
fu_elantp_haptic_firmware_get_driver_ic(FuElantpHapticFirmware *self);
