/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-rts54hub-device.h"
#include "fu-rts54hub-rtd21xx-background.h"
#include "fu-rts54hub-rtd21xx-foreground.h"

static void
fu_plugin_rts54hub_init(FuPlugin *plugin)
{
	fu_plugin_add_device_gtype(plugin, FU_TYPE_RTS54HUB_DEVICE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_RTS54HUB_RTD21XX_BACKGROUND);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_RTS54HUB_RTD21XX_FOREGROUND);
}

static void
fu_plugin_rts54hub_load(FuContext *ctx)
{
	fu_context_add_quirk_key(ctx, "Rts54TargetAddr");
	fu_context_add_quirk_key(ctx, "Rts54I2cSpeed");
	fu_context_add_quirk_key(ctx, "Rts54RegisterAddrLen");
}

void
fu_plugin_init_vfuncs(FuPluginVfuncs *vfuncs)
{
	vfuncs->build_hash = FU_BUILD_HASH;
	vfuncs->load = fu_plugin_rts54hub_load;
	vfuncs->init = fu_plugin_rts54hub_init;
}
