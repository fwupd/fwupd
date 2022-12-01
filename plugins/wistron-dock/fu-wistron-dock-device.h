/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_WISTRON_DOCK_DEVICE (fu_wistron_dock_device_get_type())
G_DECLARE_FINAL_TYPE(FuWistronDockDevice,
		     fu_wistron_dock_device,
		     FU,
		     WISTRON_DOCK_DEVICE,
		     FuHidDevice)
