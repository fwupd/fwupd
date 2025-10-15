/*
 * Copyright 2025 lazro <li@shzj.cc>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_LEGION_GO2_DEVICE (fu_legion_go2_device_get_type())
G_DECLARE_FINAL_TYPE(FuLegionGo2Device,
		     fu_legion_go2_device,
		     FU,
		     LEGION_GO2_DEVICE,
		     FuHidrawDevice)
