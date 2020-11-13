/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-plugin.h"

#define FU_TYPE_BCM57XX_DEVICE (fu_bcm57xx_device_get_type ())
G_DECLARE_FINAL_TYPE (FuBcm57xxDevice, fu_bcm57xx_device, FU, BCM57XX_DEVICE, FuUdevDevice)
