/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-plugin.h"

#define FU_TYPE_FASTBOOT_DEVICE (fu_fastboot_device_get_type ())
G_DECLARE_FINAL_TYPE (FuFastbootDevice, fu_fastboot_device, FU, FASTBOOT_DEVICE, FuUsbDevice)
