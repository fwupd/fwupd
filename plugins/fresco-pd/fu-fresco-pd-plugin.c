/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-fresco-pd-device.h"
#include "fu-fresco-pd-firmware.h"
#include "fu-fresco-pd-plugin.h"

struct _FuFrescoPdPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuFrescoPdPlugin, fu_fresco_pd_plugin, FU_TYPE_PLUGIN)

static void
fu_fresco_pd_plugin_init(FuFrescoPdPlugin *self)
{
}

static void
fu_fresco_pd_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_FRESCO_PD_DEVICE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_FRESCO_PD_FIRMWARE);
}

static void
fu_fresco_pd_plugin_class_init(FuFrescoPdPluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->constructed = fu_fresco_pd_plugin_constructed;
}
