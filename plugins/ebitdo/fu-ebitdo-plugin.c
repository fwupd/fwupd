/*
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-ebitdo-device.h"
#include "fu-ebitdo-firmware.h"
#include "fu-ebitdo-plugin.h"

struct _FuEbitdoPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuEbitdoPlugin, fu_ebitdo_plugin, FU_TYPE_PLUGIN)

static void
fu_ebitdo_plugin_init(FuEbitdoPlugin *self)
{
}

static void
fu_ebitdo_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_EBITDO_DEVICE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_EBITDO_FIRMWARE);
}

static void
fu_ebitdo_plugin_class_init(FuEbitdoPluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->constructed = fu_ebitdo_plugin_constructed;
}
