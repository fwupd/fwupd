/*
 * Copyright (C) 2022 Advanced Micro Devices Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-amd-pmc-device.h"

static void
fu_plugin_amd_pmc_init(FuPlugin *plugin)
{
	fu_plugin_add_udev_subsystem(plugin, "platform");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_AMD_PMC_DEVICE);
}

void
fu_plugin_init_vfuncs(FuPluginVfuncs *vfuncs)
{
	vfuncs->build_hash = FU_BUILD_HASH;
	vfuncs->init = fu_plugin_amd_pmc_init;
}
