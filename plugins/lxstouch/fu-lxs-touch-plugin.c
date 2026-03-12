/*
 * Copyright 2026 LXS <support@lxsemicon.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-lxs-touch-device.h"

static void
fu_plugin_lxs_touch_init(FuPlugin *plugin)
{
	fu_plugin_add_device_gtype(plugin, FU_TYPE_LXS_TOUCH_DEVICE);
	fu_plugin_add_udev_subsystem(plugin, "hidraw");
}

void
fu_plugin_init_vfuncs(FuPluginVfuncs *vfuncs)
{
	vfuncs->build_hash = FU_BUILD_HASH;
	vfuncs->init = fu_plugin_lxs_touch_init;
}
