/*
 * Copyright (C) 2021 Daniel Campello <campello@chromium.org>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-tuxec-device.h"

#define FU_TYPE_TUXEC_INTERNAL_DEVICE (fu_tuxec_internal_device_get_type ())
G_DECLARE_FINAL_TYPE (FuTuxecInternalDevice, fu_tuxec_internal_device, FU,
		      TUXEC_INTERNAL_DEVICE, FuTuxecDevice)

FuDevice	*fu_tuxec_internal_device_new			(void);
