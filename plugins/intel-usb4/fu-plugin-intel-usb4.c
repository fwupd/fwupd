/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-intel-usb4-device.h"
#include "fu-intel-usb4-firmware.h"

static void
fu_plugin_intel_usb4_init(FuPlugin *plugin)
{
	fu_plugin_add_device_gtype(plugin, FU_TYPE_INTEL_USB4_DEVICE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_INTEL_USB4_FIRMWARE);
}

void
fu_plugin_init_vfuncs(FuPluginVfuncs *vfuncs)
{
	vfuncs->build_hash = FU_BUILD_HASH;
	vfuncs->init = fu_plugin_intel_usb4_init;
}
