/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

G_DECLARE_FINAL_TYPE(FuLinuxLockdownPlugin,
		     fu_linux_lockdown_plugin,
		     FU,
		     LINUX_LOCKDOWN_PLUGIN,
		     FuPlugin)
