/*
 * Copyright 2026 SHENZHEN Betterlife Electronic CO.LTD <xiaomaoting@blestech.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_BLESTECH_TP_HID_DEVICE (fu_blestech_tp_hid_device_get_type())
G_DECLARE_FINAL_TYPE(FuBlestechTpHidDevice,
		     fu_blestech_tp_hid_device,
		     FU,
		     BLESTECH_TP_HID_DEVICE,
		     FuHidrawDevice)
