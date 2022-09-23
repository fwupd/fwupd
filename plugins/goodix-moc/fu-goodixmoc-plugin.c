/*
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2020 boger wang <boger@goodix.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-goodixmoc-device.h"
#include "fu-goodixmoc-plugin.h"

struct _FuGoodixMocPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuGoodixMocPlugin, fu_goodixmoc_plugin, FU_TYPE_PLUGIN)

static void
fu_goodixmoc_plugin_init(FuGoodixMocPlugin *self)
{
}

static void
fu_goodixmoc_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_GOODIXMOC_DEVICE);
}

static void
fu_goodixmoc_plugin_class_init(FuGoodixMocPluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->constructed = fu_goodixmoc_plugin_constructed;
}
