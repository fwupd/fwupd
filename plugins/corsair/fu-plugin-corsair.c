/*
 * Copyright (C) 2022 Andrii Dushko <andrii.dushko@developex.net>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-corsair-device.h"

static void
fu_plugin_corsair_init(FuPlugin *plugin)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_CORSAIR_DEVICE);
	fu_context_add_quirk_key(ctx, "CorsairDeviceKind");
	fu_context_add_quirk_key(ctx, "CorsairVendorInterfaceId");
}

void
fu_plugin_init_vfuncs(FuPluginVfuncs *vfuncs)
{
	vfuncs->build_hash = FU_BUILD_HASH;
	vfuncs->init = fu_plugin_corsair_init;
}
