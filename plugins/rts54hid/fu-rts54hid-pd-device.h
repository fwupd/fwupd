/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-rts54hid-device.h"

#define FU_TYPE_RTS54HID_PD_DEVICE (fu_rts54hid_pd_device_get_type())
G_DECLARE_FINAL_TYPE(FuRts54hidPdDevice,
		     fu_rts54hid_pd_device,
		     FU,
		     RTS54HID_PD_DEVICE,
		     FuRts54hidDevice)
