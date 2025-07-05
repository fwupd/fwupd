/*
 * Copyright 2024 Logitech, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_LOGITECH_TAP_TOUCH_FIRMWARE (fu_logitech_tap_touch_firmware_get_type())

G_DECLARE_FINAL_TYPE(FuLogitechTapTouchFirmware,
		     fu_logitech_tap_touch_firmware,
		     FU,
		     LOGITECH_TAP_TOUCH_FIRMWARE,
		     FuFirmware)

#define FU_LOGITECH_TAP_TOUCH_MAX_FW_FILE_SIZE (256 * 1024)
#define FU_LOGITECH_TAP_TOUCH_MIN_FW_FILE_SIZE 0x6600

guint16
fu_logitech_tap_touch_firmware_get_ap_checksum(FuLogitechTapTouchFirmware *self);
guint16
fu_logitech_tap_touch_firmware_get_df_checksum(FuLogitechTapTouchFirmware *self);
