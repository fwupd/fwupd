/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-solokey-device.h"
#include "fu-solokey-firmware.h"

static void
fu_plugin_solokey_init(FuPlugin *plugin)
{
	fu_plugin_add_device_gtype(plugin, FU_TYPE_SOLOKEY_DEVICE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_SOLOKEY_FIRMWARE);
}

void
fu_plugin_init_vfuncs(FuPluginVfuncs *vfuncs)
{
	vfuncs->build_hash = FU_BUILD_HASH;
	vfuncs->init = fu_plugin_solokey_init;
}
