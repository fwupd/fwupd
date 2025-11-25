/*
 * Copyright 2025 Framework Computer Inc
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-framework-qmk-device.h"
#include "fu-framework-qmk-plugin.h"

struct _FuFrameworkQmkPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuFrameworkQmkPlugin, fu_framework_qmk_plugin, FU_TYPE_PLUGIN)

static void
fu_framework_qmk_plugin_init(FuFrameworkQmkPlugin *self)
{
}

static void
fu_framework_qmk_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_FRAMEWORK_QMK_DEVICE);
	fu_plugin_add_udev_subsystem(plugin, "hidraw");
}

static void
fu_framework_qmk_plugin_class_init(FuFrameworkQmkPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_framework_qmk_plugin_constructed;
}
