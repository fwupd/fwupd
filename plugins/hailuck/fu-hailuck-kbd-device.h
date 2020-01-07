/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-plugin.h"
#include "fu-hid-device.h"

#define FU_TYPE_HAILUCK_KBD_DEVICE (fu_hailuck_kbd_device_get_type ())
G_DECLARE_FINAL_TYPE (FuHaiLuckKbdDevice, fu_hailuck_kbd_device, FU, HAILUCK_KBD_DEVICE, FuHidDevice)
