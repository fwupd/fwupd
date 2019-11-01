/*
 * Copyright (C) 2016-2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-plugin.h"

#define FU_TYPE_ALTOS_DEVICE (fu_altos_device_get_type ())
G_DECLARE_FINAL_TYPE (FuAltosDevice, fu_altos_device, FU, ALTOS_DEVICE, FuUsbDevice)
