/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-intel-me-mca-device.h"
#include "fu-intel-me-mkhi-device.h"
#include "fu-intel-me-plugin.h"

struct _FuIntelMePlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuIntelMePlugin, fu_intel_me_plugin, FU_TYPE_PLUGIN)

static void
fu_intel_me_plugin_init(FuIntelMePlugin *self)
{
}

static void
fu_intel_me_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_udev_subsystem(plugin, "mei");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_INTEL_ME_MCA_DEVICE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_INTEL_ME_MKHI_DEVICE);
}

static void
fu_intel_me_plugin_class_init(FuIntelMePluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->constructed = fu_intel_me_plugin_constructed;
}
