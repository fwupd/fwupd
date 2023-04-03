/*
 * Copyright (C) 2019 Mario Limonciello <mario.limonciello@dell.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-emmc-device.h"
#include "fu-emmc-plugin.h"

struct _FuEmmcPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuEmmcPlugin, fu_emmc_plugin, FU_TYPE_PLUGIN)

static void
fu_emmc_plugin_init(FuEmmcPlugin *self)
{
}

static void
fu_emmc_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_udev_subsystem(plugin, "block");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_EMMC_DEVICE);
}

static void
fu_emmc_plugin_class_init(FuEmmcPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_emmc_plugin_constructed;
}
