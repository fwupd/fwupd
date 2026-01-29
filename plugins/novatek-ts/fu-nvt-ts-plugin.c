/*
 * Copyright 2026 Novatekmsp <novatekmsp@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "config.h"

#include "fu-nvt-ts-device.h"

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "FuPluginNvtTs"

#define NVT_TS_PLUGIN_VERSION "2.1.0"

struct _FuNvtTsPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuNvtTsPlugin, fu_nvt_ts_plugin, FU_TYPE_PLUGIN)

static void
fu_nvt_ts_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin;

	plugin = FU_PLUGIN(obj);

	if (fu_plugin_get_name(plugin) == NULL)
		fwupd_plugin_set_name(FWUPD_PLUGIN(plugin), "novatek_ts");

	fu_plugin_add_device_udev_subsystem(plugin, "hidraw");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_NVT_TS_DEVICE);

	/* fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_NVT_TS_FIRMWARE); */
}

static void
fu_nvt_ts_plugin_class_init(FuNvtTsPluginClass *klass)
{
	GObjectClass *object_class;

	g_info("plugin class init");

	object_class = G_OBJECT_CLASS(klass);
	object_class->constructed = fu_nvt_ts_plugin_constructed;
}

static void
fu_nvt_ts_plugin_init(FuNvtTsPlugin *self)
{
	g_info("plugin init, plugin version %s", NVT_TS_PLUGIN_VERSION);
}
