/*
 * Copyright 2024 Randy Lai <randy.lai@weidahitech.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_WEIDA_RAW_DEVICE (fu_weida_raw_device_get_type())
G_DECLARE_FINAL_TYPE(FuWeidaRawDevice, fu_weida_raw_device, FU, WEIDA_RAW_DEVICE, FuUdevDevice)
