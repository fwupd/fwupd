/*
 * Copyright 2025 Shihwei Huang <shihwei.huang@focaltech-electronics.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_FOCALTOUCH_FIRMWARE (fu_focaltouch_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuFocaltouchFirmware,
		     fu_focaltouch_firmware,
		     FU,
		     FOCALTOUCH_FIRMWARE,
		     FuFirmware)

guint32
fu_focaltouch_firmware_get_checksum(FuFocaltouchFirmware *self);
