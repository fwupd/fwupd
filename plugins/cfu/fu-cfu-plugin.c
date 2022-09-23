/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-cfu-device.h"
#include "fu-cfu-plugin.h"

struct _FuCfuPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuCfuPlugin, fu_cfu_plugin, FU_TYPE_PLUGIN)

static void
fu_cfu_plugin_init(FuCfuPlugin *self)
{
}

static void
fu_cfu_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_CFU_DEVICE);
}

static void
fu_cfu_plugin_class_init(FuCfuPluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->constructed = fu_cfu_plugin_constructed;
}
