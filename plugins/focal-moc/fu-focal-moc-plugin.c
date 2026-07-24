/*
 * Copyright 2024 FocalTech Systems Co., Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-focal-moc-device.h"
#include "fu-focal-moc-plugin.h"

struct _FuFocalMocPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuFocalMocPlugin, fu_focal_moc_plugin, FU_TYPE_PLUGIN)

static void
fu_focal_moc_plugin_init(FuFocalMocPlugin *self)
{
}

static void
fu_focal_moc_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_udev_subsystem(plugin, "usb");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_FOCAL_MOC_DEVICE);

	/* chain up to parent */
	G_OBJECT_CLASS(fu_focal_moc_plugin_parent_class)->constructed(obj);
}

static void
fu_focal_moc_plugin_class_init(FuFocalMocPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_focal_moc_plugin_constructed;
}
