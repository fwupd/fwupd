/*
 * Copyright (C) 2021 Daniel Campello <campello@chromium.org>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-flashrom-device.h"

#define FU_TYPE_FLASHROM_INTERNAL_DEVICE (fu_flashrom_internal_device_get_type ())
G_DECLARE_FINAL_TYPE (FuFlashromInternalDevice, fu_flashrom_internal_device, FU,
		      FLASHROM_INTERNAL_DEVICE, FuFlashromDevice)

FuDevice	*fu_flashrom_internal_device_new			(void);
