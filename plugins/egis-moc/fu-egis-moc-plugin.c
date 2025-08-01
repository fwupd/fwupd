/*
 * Copyright 2025 Jason Huang <jason.huang@egistec.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-egis-moc-device.h"
#include "fu-egis-moc-plugin.h"

struct _FuEgisMocPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuEgisMocPlugin, fu_egis_moc_plugin, FU_TYPE_PLUGIN)

static void
fu_egis_moc_plugin_init(FuEgisMocPlugin *self)
{
}

static void
fu_egis_moc_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_EGIS_MOC_DEVICE);
}

static void
fu_egis_moc_plugin_class_init(FuEgisMocPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_egis_moc_plugin_constructed;
}
