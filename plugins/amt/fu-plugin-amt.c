/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-amt-device.h"

static void
fu_plugin_init(FuPlugin *plugin)
{
	fu_plugin_add_udev_subsystem(plugin, "mei");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_AMT_DEVICE);
}

void
fu_plugin_init_vfuncs(FuPluginVfuncs *vfuncs)
{
	vfuncs->init = fu_plugin_init;
	vfuncs->build_hash = FU_BUILD_HASH;
}
