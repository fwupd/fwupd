/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

G_DECLARE_FINAL_TYPE(FuParadeUsbhubPlugin,
		     fu_parade_usbhub_plugin,
		     FU,
		     PARADE_USBHUB_PLUGIN,
		     FuPlugin)
