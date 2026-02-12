/*
 * Copyright 2026 Novatekmsp <novatekmsp@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "config.h"

#include "fu-novatek-ts-device.h"
#include "fu-novatek-ts-firmware.h"
#include "fu-novatek-ts-plugin.h"

struct _FuNovatekTsPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuNovatekTsPlugin, fu_novatek_ts_plugin, FU_TYPE_PLUGIN)

static void
fu_novatek_ts_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_device_udev_subsystem(plugin, "hidraw");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_NOVATEK_TS_DEVICE);
	fu_plugin_add_firmware_gtype(plugin, FU_TYPE_NOVATEK_TS_FIRMWARE);
}

static void
fu_novatek_ts_plugin_class_init(FuNovatekTsPluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->constructed = fu_novatek_ts_plugin_constructed;
}

static void
fu_novatek_ts_plugin_init(FuNovatekTsPlugin *self)
{
}
