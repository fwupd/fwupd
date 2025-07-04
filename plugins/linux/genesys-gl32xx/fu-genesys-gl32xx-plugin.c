/*
 * Copyright 2023 Denis Pynkin <denis.pynkin@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-genesys-gl32xx-device.h"
#include "fu-genesys-gl32xx-firmware.h"
#include "fu-genesys-gl32xx-plugin.h"

struct _FuGenesysGl32xxPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuGenesysGl32xxPlugin, fu_genesys_gl32xx_plugin, FU_TYPE_PLUGIN)

static void
fu_genesys_gl32xx_plugin_init(FuGenesysGl32xxPlugin *self)
{
}

static void
fu_genesys_gl32xx_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	FuContext *ctx = fu_plugin_get_context(plugin);
	fu_plugin_add_udev_subsystem(plugin, "block");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_GENESYS_GL32XX_DEVICE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_GENESYS_GL32XX_FIRMWARE);
	fu_context_add_quirk_key(ctx, "GenesysGl32xxCompatibleModel");
}

static void
fu_genesys_gl32xx_plugin_class_init(FuGenesysGl32xxPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_genesys_gl32xx_plugin_constructed;
}
