/*
 * Copyright 2025 Shihwei Huang <shihwei.huang@focaltech-electronics.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_FOCAL_TOUCH_FIRMWARE (fu_focal_touch_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuFocalTouchFirmware,
		     fu_focal_touch_firmware,
		     FU,
		     FOCAL_TOUCH_FIRMWARE,
		     FuFirmware)

guint32
fu_focal_touch_firmware_get_checksum(FuFocalTouchFirmware *self);
