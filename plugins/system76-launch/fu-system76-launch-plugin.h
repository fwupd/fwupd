/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

G_DECLARE_FINAL_TYPE(FuSystem76LaunchPlugin,
		     fu_system76_launch_plugin,
		     FU,
		     SYSTEM76_LAUNCH_PLUGIN,
		     FuPlugin)
