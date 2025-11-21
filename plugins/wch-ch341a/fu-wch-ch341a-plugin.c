/*
 * Copyright 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "fu-wch-ch341a-plugin.h"

#include "config.h"
#include "fu-wch-ch341a-device.h"

struct _FuWchCh341aPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuWchCh341aPlugin, fu_wch_ch341a_plugin, FU_TYPE_PLUGIN)

static void
fu_wch_ch341a_plugin_init(FuWchCh341aPlugin *self)
{
}

static void
fu_wch_ch341a_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_udev_subsystem(plugin, "usb");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_WCH_CH341A_DEVICE);
}

static void
fu_wch_ch341a_plugin_class_init(FuWchCh341aPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_wch_ch341a_plugin_constructed;
}
