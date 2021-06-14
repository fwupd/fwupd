/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_RTS54HID_DEVICE (fu_rts54hid_device_get_type ())
G_DECLARE_FINAL_TYPE (FuRts54HidDevice, fu_rts54hid_device, FU, RTS54HID_DEVICE, FuHidDevice)

#define FU_RTS54HID_DEVICE_TIMEOUT			1000 /* ms */
