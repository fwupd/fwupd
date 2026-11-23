/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-realtek-rts54hid-device.h"

#define FU_TYPE_REALTEK_RTS54HID_HUB_DEVICE (fu_realtek_rts54hid_hub_device_get_type())
G_DECLARE_FINAL_TYPE(FuRealtekRts54hidHubDevice,
		     fu_realtek_rts54hid_hub_device,
		     FU,
		     REALTEK_RTS54HID_HUB_DEVICE,
		     FuRealtekRts54hidDevice)
