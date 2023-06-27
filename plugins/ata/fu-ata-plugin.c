/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-ata-device.h"
#include "fu-ata-plugin.h"

struct _FuAtaPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuAtaPlugin, fu_ata_plugin, FU_TYPE_PLUGIN)

static void
fu_ata_plugin_init(FuAtaPlugin *self)
{
}

static void
fu_ata_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_device_udev_subsystem(plugin, "block");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_ATA_DEVICE);
}

static void
fu_ata_plugin_class_init(FuAtaPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_ata_plugin_constructed;
}
