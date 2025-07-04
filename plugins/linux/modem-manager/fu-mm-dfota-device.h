/*
 * Copyright 2024 TDT AG <development@tdt.de>
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-mm-device.h"

#define FU_TYPE_MM_DFOTA_DEVICE (fu_mm_dfota_device_get_type())
G_DECLARE_FINAL_TYPE(FuMmDfotaDevice, fu_mm_dfota_device, FU, MM_DFOTA_DEVICE, FuMmDevice)
