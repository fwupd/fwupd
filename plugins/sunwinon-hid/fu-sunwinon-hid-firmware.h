/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-sunwinon-hid-struct.h"

#define FU_TYPE_SUNWINON_HID_FIRMWARE (fu_sunwinon_hid_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuSunwinonHidFirmware,
		     fu_sunwinon_hid_firmware,
		     FU,
		     SUNWINON_HID_FIRMWARE,
		     FuFirmware)

guint32
fu_sunwinon_hid_firmware_get_full_checksum(FuSunwinonHidFirmware *self) G_GNUC_NON_NULL(1);
FuSunwinonFwType
fu_sunwinon_hid_firmware_get_fw_type(FuSunwinonHidFirmware *self) G_GNUC_NON_NULL(1);
