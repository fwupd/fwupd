/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

G_DECLARE_FINAL_TYPE(FuLogitechHidppPlugin,
		     fu_logitech_hidpp_plugin,
		     FU,
		     LOGITECH_HIDPP_PLUGIN,
		     FuPlugin)
