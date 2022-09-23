/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

G_DECLARE_FINAL_TYPE(FuLinuxTaintedPlugin,
		     fu_linux_tainted_plugin,
		     FU,
		     LINUX_TAINTED_PLUGIN,
		     FuPlugin)
