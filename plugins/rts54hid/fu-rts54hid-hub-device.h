/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-rts54hid-device.h"

#define FU_TYPE_RTS54HID_HUB_DEVICE (fu_rts54hid_hub_device_get_type())
G_DECLARE_FINAL_TYPE(FuRts54hidHubDevice,
		     fu_rts54hid_hub_device,
		     FU,
		     RTS54HID_HUB_DEVICE,
		     FuRts54hidDevice)
