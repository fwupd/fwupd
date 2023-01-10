/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-device.h"

#define FU_TYPE_FDT_DEVICE (fu_fdt_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuFdtDevice, fu_fdt_device, FU, FDT_DEVICE, FuDevice)

struct _FuFdtDeviceClass {
	FuDeviceClass parent_class;
};
