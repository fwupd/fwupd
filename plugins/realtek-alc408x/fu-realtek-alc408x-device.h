/*
 * Copyright 2026 NVIDIA Corporation
 * Author: Vishnu Raghav <vraghav@nvidia.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_REALTEK_ALC408X_DEVICE (fu_realtek_alc408x_device_get_type())
G_DECLARE_FINAL_TYPE(FuRealtekAlc408xDevice,
		     fu_realtek_alc408x_device,
		     FU,
		     REALTEK_ALC408X_DEVICE,
		     FuUsbDevice)
