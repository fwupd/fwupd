/*
 * Copyright (C) 2023 Goodix.inc <xulinkun@goodix.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_GOODIXTP_HID_DEVICE (fu_goodixtp_hid_device_get_type())
G_DECLARE_FINAL_TYPE(FuGoodixtpHidDevice,
		     fu_goodixtp_hid_device,
		     FU,
		     GOODIXTP_HID_DEVICE,
		     FuUdevDevice)
