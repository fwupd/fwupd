/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2020 Jimmy Yu <Jimmy_yu@pixart.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-plugin.h"
#include "fu-udev-device.h"
#define FU_TYPE_PXI_DEVICE (fu_pxi_device_get_type ())

G_DECLARE_DERIVABLE_TYPE (FuPxiDevice, fu_pxi_device, FU, PXI_DEVICE, FuUdevDevice)

struct _FuPxiDeviceClass
{
	FuUdevDeviceClass	parent_class;
};
