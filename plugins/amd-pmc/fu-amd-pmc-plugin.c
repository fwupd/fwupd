/*
 * Copyright (C) 2022 Advanced Micro Devices Inc.
 * All rights reserved.
 *
 * This file is provided under a dual MIT/LGPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 * AMD Chooses the MIT license part of Dual MIT/LGPLv2 license agreement.
 *
 * SPDX-License-Identifier: LGPL-2.1+ OR MIT
 */

#include "config.h"

#include "fu-amd-pmc-device.h"
#include "fu-amd-pmc-plugin.h"

struct _FuAmdPmcPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuAmdPmcPlugin, fu_amd_pmc_plugin, FU_TYPE_PLUGIN)

static void
fu_amd_pmc_plugin_init(FuAmdPmcPlugin *self)
{
}

static void
fu_amd_pmc_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_udev_subsystem(plugin, "platform");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_AMD_PMC_DEVICE);
}

static void
fu_amd_pmc_plugin_class_init(FuAmdPmcPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_amd_pmc_plugin_constructed;
}
