/*
 * Copyright 2022 Hai Su <hsu@kinet-ic.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-kinetic-dp-device.h"

#define FU_TYPE_KINETIC_DP_PUMA_DEVICE (fu_kinetic_dp_puma_device_get_type())
G_DECLARE_FINAL_TYPE(FuKineticDpPumaDevice,
		     fu_kinetic_dp_puma_device,
		     FU,
		     KINETIC_DP_PUMA_DEVICE,
		     FuKineticDpDevice)
