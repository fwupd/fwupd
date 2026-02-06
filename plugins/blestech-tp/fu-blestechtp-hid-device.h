/*
 * Copyright 2026 SHENZHEN Betterlife Electronic CO.LTD <xiaomaoting@blestech.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-blestechtp-common.h"

#define FU_TYPE_BLESTECHTP_HID_DEVICE (fu_blestechtp_hid_device_get_type())
G_DECLARE_FINAL_TYPE(FuBlestechtpHidDevice,
		     fu_blestechtp_hid_device,
		     FU,
		     BLESTECHTP_HID_DEVICE,
		     FuHidrawDevice)
