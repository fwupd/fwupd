/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-fastboot-device.h"

static void
fu_plugin_fastboot_load(FuContext *ctx)
{
	fu_context_add_quirk_key(ctx, "FastbootBlockSize");
	fu_context_add_quirk_key(ctx, "FastbootOperationDelay");
}

static void
fu_plugin_fastboot_init(FuPlugin *plugin)
{
	fu_plugin_add_device_gtype(plugin, FU_TYPE_FASTBOOT_DEVICE);
}

void
fu_plugin_init_vfuncs(FuPluginVfuncs *vfuncs)
{
	vfuncs->build_hash = FU_BUILD_HASH;
	vfuncs->load = fu_plugin_fastboot_load;
	vfuncs->init = fu_plugin_fastboot_init;
}
