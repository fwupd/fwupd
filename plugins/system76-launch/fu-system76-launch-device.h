/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2021 Jeremy Soller <jeremy@system76.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-plugin.h"

#define FU_TYPE_SYSTEM76_LAUNCH_DEVICE (fu_system76_launch_device_get_type ())
G_DECLARE_FINAL_TYPE (FuSystem76LaunchDevice, fu_system76_launch_device, FU, SYSTEM76_LAUNCH_DEVICE, FuUsbDevice)
