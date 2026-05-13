/*
 * Copyright 2019 Richard Hughes <richard@hughsie.com>
 * Copyright 2024 Advanced Micro Devices Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_AMD_KRIA_DEVICE (fu_amd_kria_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuAmdKriaDevice, fu_amd_kria_device, FU, AMD_KRIA_DEVICE, FuI2cDevice)

struct _FuAmdKriaDeviceClass {
	FuI2cDeviceClass parent_class;
};
