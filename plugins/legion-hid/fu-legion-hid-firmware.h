/*
 * Copyright 2025 hya1711 <591770796@qq.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_LEGION_HID_FIRMWARE (fu_legion_hid_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuLegionHidFirmware,
		     fu_legion_hid_firmware,
		     FU,
		     LEGION_HID_FIRMWARE,
		     FuFirmware)

#define FU_LEGION_HID_FIRMWARE_ID_MCU	NULL
#define FU_LEGION_HID_FIRMWARE_ID_LEFT	"LEFT"
#define FU_LEGION_HID_FIRMWARE_ID_RIGHT "RIGHT"
