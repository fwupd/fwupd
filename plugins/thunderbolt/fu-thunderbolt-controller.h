/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-thunderbolt-device.h"

#define FU_TYPE_THUNDERBOLT_CONTROLLER (fu_thunderbolt_controller_get_type())
G_DECLARE_FINAL_TYPE(FuThunderboltController,
		     fu_thunderbolt_controller,
		     FU,
		     THUNDERBOLT_CONTROLLER,
		     FuThunderboltDevice)
