/*
 * Copyright 2026 Himax Company, Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_HIMAXTP_HID_DEVICE (fu_himaxtp_hid_device_get_type())
G_DECLARE_FINAL_TYPE(FuHimaxtpHidDevice,
		     fu_himaxtp_hid_device,
		     FU,
		     HIMAXTP_HID_DEVICE,
		     FuHidrawDevice)
