/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-ep963x-device.h"
#include "fu-ep963x-firmware.h"
#include "fu-ep963x-plugin.h"

struct _FuEp963XPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuEp963XPlugin, fu_ep963x_plugin, FU_TYPE_PLUGIN)

static void
fu_ep963x_plugin_init(FuEp963XPlugin *self)
{
}

static void
fu_ep963x_plugin_object_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_set_name(plugin, "ep963x");
}

static void
fu_ep963x_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_EP963X_DEVICE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_EP963X_FIRMWARE);
}

static void
fu_ep963x_plugin_class_init(FuEp963XPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->constructed = fu_ep963x_plugin_object_constructed;
	plugin_class->constructed = fu_ep963x_plugin_constructed;
}
