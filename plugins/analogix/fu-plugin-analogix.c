/*
 * Copyright (C) 2021 Xiaotian Cui <xtcui@analogixsemi.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-analogix-device.h"
#include "fu-analogix-firmware.h"

static void
fu_plugin_analogix_init(FuPlugin *plugin)
{
	fu_plugin_add_device_gtype(plugin, FU_TYPE_ANALOGIX_DEVICE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_ANALOGIX_FIRMWARE);
}

void
fu_plugin_init_vfuncs(FuPluginVfuncs *vfuncs)
{
	vfuncs->build_hash = FU_BUILD_HASH;
	vfuncs->init = fu_plugin_analogix_init;
}
