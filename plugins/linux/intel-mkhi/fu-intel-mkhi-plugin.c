/*
 * Copyright 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-intel-mkhi-device.h"
#include "fu-intel-mkhi-plugin.h"

struct _FuIntelMkhiPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuIntelMkhiPlugin, fu_intel_mkhi_plugin, FU_TYPE_PLUGIN)

static void
fu_intel_mkhi_plugin_init(FuIntelMkhiPlugin *self)
{
}

static void
fu_intel_mkhi_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_udev_subsystem(plugin, "mei");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_INTEL_MKHI_DEVICE);
}

static void
fu_intel_mkhi_plugin_class_init(FuIntelMkhiPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_intel_mkhi_plugin_constructed;
}
