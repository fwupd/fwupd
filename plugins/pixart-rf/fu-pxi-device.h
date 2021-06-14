/*
 * Copyright (C) 2020 Jimmy Yu <Jimmy_yu@pixart.com>
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_PXI_DEVICE (fu_pxi_device_get_type ())

G_DECLARE_FINAL_TYPE (FuPxiDevice, fu_pxi_device, FU, PXI_DEVICE, FuUdevDevice)
