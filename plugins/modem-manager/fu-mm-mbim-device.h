/*
 * Copyright 2021 Jarvis Jiang <jarvis.w.jiang@gmail.com>
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-mm-device.h"

#define FU_TYPE_MM_MBIM_DEVICE (fu_mm_mbim_device_get_type())
G_DECLARE_FINAL_TYPE(FuMmMbimDevice, fu_mm_mbim_device, FU, MM_MBIM_DEVICE, FuMmDevice)
