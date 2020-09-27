/*
 * Copyright (C) 2016-2017 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2020 boger wang <boger@goodix.com>
 * 
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-plugin.h"

#define FU_TYPE_GOODIXFP_DEVICE (fu_goodixfp_device_get_type ())
G_DECLARE_FINAL_TYPE (FuGoodixFpDevice, fu_goodixfp_device, FU, GOODIXFP_DEVICE, FuUsbDevice)
