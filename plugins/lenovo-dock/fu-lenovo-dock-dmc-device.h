/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_LENOVO_DOCK_DMC_DEVICE (fu_lenovo_dock_dmc_device_get_type())
G_DECLARE_FINAL_TYPE(FuLenovoDockDmcDevice,
		     fu_lenovo_dock_dmc_device,
		     FU,
		     LENOVO_DOCK_DMC_DEVICE,
		     FuUsbDevice)
