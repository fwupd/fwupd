/*
 * Copyright (c) 1999-2022 Logitech, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-logitech-scribe-device.h"
#include "fu-logitech-scribe-plugin.h"

struct _FuLogitechScribePlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuLogitechScribePlugin, fu_logitech_scribe_plugin, FU_TYPE_PLUGIN)

static void
fu_logitech_scribe_plugin_init(FuLogitechScribePlugin *self)
{
}

static void
fu_logitech_scribe_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_udev_subsystem(plugin, "video4linux");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_LOGITECH_SCRIBE_DEVICE);
}

static void
fu_logitech_scribe_plugin_class_init(FuLogitechScribePluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_logitech_scribe_plugin_constructed;
}
