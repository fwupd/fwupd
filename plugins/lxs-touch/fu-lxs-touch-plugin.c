/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-lxstouch-device.h"
#include "fu-lxstouch-firmware.h"
#include "fu-lxstouch-plugin.h"

struct _FuLxstouchPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuLxstouchPlugin, fu_lxstouch_plugin, FU_TYPE_PLUGIN)

static void
fu_lxstouch_plugin_init(FuLxstouchPlugin *self)
{
}

static void
fu_lxstouch_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_udev_subsystem(plugin, "hidraw");
	fu_plugin_add_firmware_gtype(plugin, FU_TYPE_LXSTOUCH_FIRMWARE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_LXSTOUCH_DEVICE);
}

static void
fu_lxstouch_plugin_class_init(FuLxstouchPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_lxstouch_plugin_constructed;
}
