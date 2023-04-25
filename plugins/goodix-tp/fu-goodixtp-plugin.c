/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-goodixtp-hid-device.h"
#include "fu-goodixtp-plugin.h"

struct _FuGoodixtpPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuGoodixtpPlugin, fu_goodixtp_plugin, FU_TYPE_PLUGIN)

static void
fu_goodixtp_plugin_init(FuGoodixtpPlugin *self)
{
}

static void
fu_goodixtp_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_udev_subsystem(plugin, "hidraw");
	fu_plugin_set_name(plugin, "goodixtp");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_GOODIXTP_HID_DEVICE);
}

static void
fu_goodixtp_plugin_class_init(FuGoodixtpPluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->constructed = fu_goodixtp_plugin_constructed;
}
