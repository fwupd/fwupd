/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-rts54hid-device.h"
#include "fu-rts54hid-module.h"

void
fu_plugin_init (FuPlugin *plugin)
{
	FuContext *ctx = fu_plugin_get_context (plugin);
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_plugin_add_device_gtype (plugin, FU_TYPE_RTS54HID_DEVICE);
	fu_plugin_add_device_gtype (plugin, FU_TYPE_RTS54HID_MODULE);
	fu_context_add_quirk_key (ctx, "Rts54TargetAddr");
	fu_context_add_quirk_key (ctx, "Rts54I2cSpeed");
	fu_context_add_quirk_key (ctx, "Rts54RegisterAddrLen");
}
