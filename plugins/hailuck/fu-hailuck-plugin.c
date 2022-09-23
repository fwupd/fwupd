/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-hailuck-bl-device.h"
#include "fu-hailuck-kbd-device.h"
#include "fu-hailuck-kbd-firmware.h"
#include "fu-hailuck-plugin.h"

struct _FuHailuckPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuHailuckPlugin, fu_hailuck_plugin, FU_TYPE_PLUGIN)

static void
fu_hailuck_plugin_init(FuHailuckPlugin *self)
{
}

static void
fu_hailuck_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_HAILUCK_KBD_FIRMWARE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_HAILUCK_BL_DEVICE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_HAILUCK_KBD_DEVICE);
}

static void
fu_hailuck_plugin_class_init(FuHailuckPluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->constructed = fu_hailuck_plugin_constructed;
}
