/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>
#include "fu-fastboot-device.h"

#define FU_TYPE_FASTBOOT_ROLLING_DEVICE (fu_fastboot_rolling_device_get_type())
G_DECLARE_FINAL_TYPE(FuFastbootRollingDevice, fu_fastboot_rolling_device, FU, FASTBOOT_ROLLING_DEVICE, FuFastbootDevice)

