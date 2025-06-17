/*
 * Copyright 2025 NVIDIA Corporation & Affiliates
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

G_DECLARE_FINAL_TYPE(FuDevlinkPlugin,
		     fu_devlink_plugin,
		     FU,
		     DEVLINK_PLUGIN,
		     FuPlugin)
