/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_AVER_HID_FIRMWARE (fu_aver_hid_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuAverHidFirmware, fu_aver_hid_firmware, FU, AVER_HID_FIRMWARE, FuFirmware)

FuFirmware *
fu_aver_hid_firmware_new(void);
