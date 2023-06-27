/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-uf2-device.h"
#include "fu-uf2-firmware.h"
#include "fu-uf2-plugin.h"

struct _FuUf2Plugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuUf2Plugin, fu_uf2_plugin, FU_TYPE_PLUGIN)

static void
fu_uf2_plugin_init(FuUf2Plugin *self)
{
}

static void
fu_uf2_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_UF2_DEVICE);
	fu_plugin_add_firmware_gtype(plugin, "uf2", FU_TYPE_UF2_FIRMWARE);
	fu_plugin_add_device_udev_subsystem(plugin, "block");
}

static void
fu_uf2_plugin_class_init(FuUf2PluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_uf2_plugin_constructed;
}
