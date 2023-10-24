/*
 * Copyright (c) 1999-2023 Logitech, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-logitech-rallysystem-audio-device.h"
#include "fu-logitech-rallysystem-plugin.h"
#include "fu-logitech-rallysystem-tablehub-device.h"

struct _FuLogitechRallysystemPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuLogitechRallysystemPlugin, fu_logitech_rallysystem_plugin, FU_TYPE_PLUGIN)

static void
fu_logitech_rallysystem_plugin_init(FuLogitechRallysystemPlugin *self)
{
}

static void
fu_logitech_rallysystem_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_udev_subsystem(plugin, "hidraw");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_LOGITECH_RALLYSYSTEM_TABLEHUB_DEVICE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_LOGITECH_RALLYSYSTEM_AUDIO_DEVICE);
}

static void
fu_logitech_rallysystem_plugin_class_init(FuLogitechRallysystemPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_logitech_rallysystem_plugin_constructed;
}
