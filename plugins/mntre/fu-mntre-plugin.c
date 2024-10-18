/*
 * Copyright 2024 Chris hofstaedtler <Ch@zeha.at>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-mntre-device.h"
#include "fu-mntre-plugin.h"

struct _FuMntrePlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuMntrePlugin, fu_mntre_plugin, FU_TYPE_PLUGIN)

static void
fu_mntre_plugin_init(FuMntrePlugin *self)
{
}

static void
fu_mntre_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_MNTRE_DEVICE);
}

static void
fu_mntre_plugin_class_init(FuMntrePluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_mntre_plugin_constructed;
}
