/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-plugin.h"
#include "fu-hid-device.h"

#define FU_TYPE_HAILUCK_BL_DEVICE (fu_hailuck_bl_device_get_type ())
G_DECLARE_FINAL_TYPE (FuHaiLuckBlDevice, fu_hailuck_bl_device, FU, HAILUCK_BL_DEVICE, FuHidDevice)
