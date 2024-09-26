/*
 * Copyright 2024 Mario Limonciello <superm1@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_LEGION_HID2_FIRMWARE (fu_legion_hid2_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuLegionHid2Firmware,
		     fu_legion_hid2_firmware,
		     FU,
		     LEGION_HID2_FIRMWARE,
		     FuFirmware)

FuFirmware *
fu_legion_hid2_firmware_new(void);

guint32
fu_legion_hid2_firmware_get_sig_offset(FuFirmware *firmware);
gssize
fu_legion_hid2_firmware_get_sig_size(FuFirmware *firmware);
guint32
fu_legion_hid2_firmware_get_data_offset(FuFirmware *firmware);
gssize
fu_legion_hid2_firmware_get_data_size(FuFirmware *firmware);
guint32
fu_legion_hid2_firmware_get_version(FuFirmware *firmware);
