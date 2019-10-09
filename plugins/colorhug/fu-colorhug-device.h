/*
 * Copyright (C) 2016-2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-plugin.h"

#define FU_TYPE_COLORHUG_DEVICE (fu_colorhug_device_get_type ())
G_DECLARE_FINAL_TYPE (FuColorhugDevice, fu_colorhug_device, FU, COLORHUG_DEVICE, FuUsbDevice)
