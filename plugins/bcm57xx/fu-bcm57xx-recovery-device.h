/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-plugin.h"

#define FU_TYPE_BCM57XX_RECOVERY_DEVICE (fu_bcm57xx_recovery_device_get_type ())
G_DECLARE_FINAL_TYPE (FuBcm57xxRecoveryDevice, fu_bcm57xx_recovery_device, FU, BCM57XX_RECOVERY_DEVICE, FuUdevDevice)

FuBcm57xxRecoveryDevice	*fu_bcm57xx_recovery_device_new	(void);
