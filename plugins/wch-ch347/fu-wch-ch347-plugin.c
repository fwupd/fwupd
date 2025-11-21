/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "fu-wch-ch347-plugin.h"

#include "config.h"
#include "fu-wch-ch347-device.h"

struct _FuWchCh347Plugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuWchCh347Plugin, fu_wch_ch347_plugin, FU_TYPE_PLUGIN)

static void
fu_wch_ch347_plugin_init(FuWchCh347Plugin *self)
{
}

static void
fu_wch_ch347_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_udev_subsystem(plugin, "usb");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_WCH_CH347_DEVICE);
}

static void
fu_wch_ch347_plugin_class_init(FuWchCh347PluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_wch_ch347_plugin_constructed;
}
