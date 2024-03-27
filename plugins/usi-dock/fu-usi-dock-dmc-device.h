/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_USI_DOCK_DMC_DEVICE (fu_usi_dock_dmc_device_get_type())
G_DECLARE_FINAL_TYPE(FuUsiDockDmcDevice,
		     fu_usi_dock_dmc_device,
		     FU,
		     USI_DOCK_DMC_DEVICE,
		     FuUsbDevice)
