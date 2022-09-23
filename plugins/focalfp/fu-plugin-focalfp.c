/*
 * Copyright (C) 2022 Shihwei Huang <shihwei.huang@focaltech-electronics.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-focalfp-firmware.h"
#include "fu-focalfp-hid-device.h"

static void
fu_plugin_focalfp_init(FuPlugin *plugin)
{
	fu_plugin_add_udev_subsystem(plugin, "hidraw");
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_FOCALFP_FIRMWARE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_FOCALFP_HID_DEVICE);
}

void
fu_plugin_init_vfuncs(FuPluginVfuncs *vfuncs)
{
	vfuncs->build_hash = FU_BUILD_HASH;
	vfuncs->init = fu_plugin_focalfp_init;
}
