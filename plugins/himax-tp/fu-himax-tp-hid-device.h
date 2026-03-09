/*
 * Copyright 2026 Himax Company, Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_HIMAX_TP_HID_DEVICE (fu_himax_tp_hid_device_get_type())
G_DECLARE_FINAL_TYPE(FuHimaxTpHidDevice,
		     fu_himax_tp_hid_device,
		     FU,
		     HIMAX_TP_HID_DEVICE,
		     FuHidrawDevice)
