/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-uf2-device.h"
#include "fu-uf2-firmware.h"

static void
fu_plugin_uf2_init(FuPlugin *plugin)
{
	fu_plugin_add_device_gtype(plugin, FU_TYPE_UF2_DEVICE);
	fu_plugin_add_firmware_gtype(plugin, "uf2", FU_TYPE_UF2_FIRMWARE);
	fu_plugin_add_udev_subsystem(plugin, "block");
}

void
fu_plugin_init_vfuncs(FuPluginVfuncs *vfuncs)
{
	vfuncs->build_hash = FU_BUILD_HASH;
	vfuncs->init = fu_plugin_uf2_init;
}
