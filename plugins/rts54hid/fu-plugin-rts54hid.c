/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-rts54hid-device.h"
#include "fu-rts54hid-module.h"

static void
fu_plugin_rts54hid_init(FuPlugin *plugin)
{
	fu_plugin_add_device_gtype(plugin, FU_TYPE_RTS54HID_DEVICE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_RTS54HID_MODULE);
}

static void
fu_plugin_rts54hid_load(FuContext *ctx)
{
	fu_context_add_quirk_key(ctx, "Rts54TargetAddr");
	fu_context_add_quirk_key(ctx, "Rts54I2cSpeed");
	fu_context_add_quirk_key(ctx, "Rts54RegisterAddrLen");
}

void
fu_plugin_init_vfuncs(FuPluginVfuncs *vfuncs)
{
	vfuncs->build_hash = FU_BUILD_HASH;
	vfuncs->load = fu_plugin_rts54hid_load;
	vfuncs->init = fu_plugin_rts54hid_init;
}
