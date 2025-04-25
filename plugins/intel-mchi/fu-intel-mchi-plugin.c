/*
 * Copyright 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-intel-mchi-device.h"
#include "fu-intel-mchi-plugin.h"

struct _FuIntelMchiPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuIntelMchiPlugin, fu_intel_mchi_plugin, FU_TYPE_PLUGIN)

static void
fu_intel_mchi_plugin_init(FuIntelMchiPlugin *self)
{
}

static void
fu_intel_mchi_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_udev_subsystem(plugin, "mei");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_INTEL_MCHI_DEVICE);
}

static void
fu_intel_mchi_plugin_class_init(FuIntelMchiPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_intel_mchi_plugin_constructed;
}
