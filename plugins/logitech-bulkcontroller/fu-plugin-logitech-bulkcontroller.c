/*
 * Copyright (c) 1999-2021 Logitech, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-logitech-bulkcontroller-device.h"

static void
fu_plugin_logitech_bulkcontroller_init(FuPlugin *plugin)
{
	fu_plugin_add_device_gtype(plugin, FU_TYPE_LOGITECH_BULKCONTROLLER_DEVICE);
}

void
fu_plugin_init_vfuncs(FuPluginVfuncs *vfuncs)
{
	vfuncs->build_hash = FU_BUILD_HASH;
	vfuncs->init = fu_plugin_logitech_bulkcontroller_init;
}
