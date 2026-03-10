/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_LENOVO_DOCK_DEVICE (fu_lenovo_dock_device_get_type())
G_DECLARE_FINAL_TYPE(FuLenovoDockDevice,
		     fu_lenovo_dock_device,
		     FU,
		     LENOVO_DOCK_DEVICE,
		     FuHidrawDevice)
