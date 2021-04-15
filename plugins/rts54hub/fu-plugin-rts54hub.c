/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-plugin-vfuncs.h"

#include "fu-rts54hub-device.h"
#include "fu-rts54hub-rtd21xx-device.h"

void
fu_plugin_init (FuPlugin *plugin)
{
	FuContext *ctx = fu_plugin_get_context (plugin);
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_plugin_add_device_gtype (plugin, FU_TYPE_RTS54HUB_DEVICE);
	fu_plugin_add_device_gtype (plugin, FU_TYPE_RTS54HUB_RTD21XX_DEVICE);
	fu_context_add_quirk_key (ctx, "Rts54TargetAddr");
	fu_context_add_quirk_key (ctx, "Rts54I2cSpeed");
	fu_context_add_quirk_key (ctx, "Rts54RegisterAddrLen");
}
