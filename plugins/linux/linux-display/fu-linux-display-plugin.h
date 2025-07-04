/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

G_DECLARE_FINAL_TYPE(FuLinuxDisplayPlugin,
		     fu_linux_display_plugin,
		     FU,
		     LINUX_DISPLAY_PLUGIN,
		     FuPlugin)
