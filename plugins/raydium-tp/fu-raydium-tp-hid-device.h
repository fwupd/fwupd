/*
 * Copyright 2025 Raydium.inc <Maker.Tsai@rad-ic.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_RAYDIUM_TP_HID_DEVICE (fu_raydium_tp_hid_device_get_type())

G_DECLARE_FINAL_TYPE(FuRaydiumtpHidDevice,
		     fu_raydium_tp_hid_device,
		     FU,
		     RAYDIUM_TP_HID_DEVICE,
		     FuHidrawDevice)
