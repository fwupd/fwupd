/*
 * Copyright 2015 Richard Hughes <richard@hughsie.com>
 * Copyright 2020 boger wang <boger@goodix.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-goodix-moc-device.h"
#include "fu-goodix-moc-plugin.h"

struct _FuGoodixMocPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuGoodixMocPlugin, fu_goodix_moc_plugin, FU_TYPE_PLUGIN)

static void
fu_goodix_moc_plugin_init(FuGoodixMocPlugin *self)
{
}

static void
fu_goodix_moc_plugin_object_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_set_name(plugin, "goodixmoc");
}

static void
fu_goodix_moc_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_GOODIX_MOC_DEVICE);
}

static void
fu_goodix_moc_plugin_class_init(FuGoodixMocPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->constructed = fu_goodix_moc_plugin_object_constructed;
	plugin_class->constructed = fu_goodix_moc_plugin_constructed;
}
