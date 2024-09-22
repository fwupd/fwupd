/*
 * Copyright 2024 Mario Limonciello <superm1@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_ASUS_HID_FIRMWARE (fu_asus_hid_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuAsusHidFirmware, fu_asus_hid_firmware, FU, ASUS_HID_FIRMWARE, FuFirmware)

const gchar *
fu_asus_hid_firmware_get_version(FuFirmware *firmware);

const gchar *
fu_asus_hid_firmware_get_product(FuFirmware *firmware);

FuFirmware *
fu_asus_hid_firmware_new(void);
