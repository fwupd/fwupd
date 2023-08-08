/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_AVER_HID_DEVICE (fu_aver_hid_device_get_type())
G_DECLARE_FINAL_TYPE(FuAverHidDevice, fu_aver_hid_device, FU, AVER_HID_DEVICE, FuHidDevice)
