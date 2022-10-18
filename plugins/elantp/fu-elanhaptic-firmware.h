/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_ELANHAPTIC_FIRMWARE (fu_elanhaptic_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuElanhapticFirmware, fu_elanhaptic_firmware, FU, ELANHAPTIC_FIRMWARE, FuFirmware)

FuFirmware *
fu_elanhaptic_firmware_new(void);

guint32
fu_elanhaptic_firmware_get_fwver(FuElanhapticFirmware *self);

guint16
fu_elanhaptic_firmware_get_driveric(FuElanhapticFirmware *self);