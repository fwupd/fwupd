/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-wacom-aes-device.h"
#include "fu-wacom-common.h"
#include "fu-wacom-emr-device.h"
#include "fu-wacom-raw-plugin.h"

struct _FuWacomRawPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuWacomRawPlugin, fu_wacom_raw_plugin, FU_TYPE_PLUGIN)

static void
fu_wacom_raw_plugin_init(FuWacomRawPlugin *self)
{
}

static void
fu_wacom_raw_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	FuContext *ctx = fu_plugin_get_context(plugin);
	fu_context_add_quirk_key(ctx, "WacomI2cFlashBlockSize");
	fu_context_add_quirk_key(ctx, "WacomI2cFlashBaseAddr");
	fu_context_add_quirk_key(ctx, "WacomI2cFlashSize");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_WACOM_AES_DEVICE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_WACOM_EMR_DEVICE);
	fu_plugin_add_udev_subsystem(plugin, "hidraw");
}

static void
fu_wacom_raw_plugin_class_init(FuWacomRawPluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->constructed = fu_wacom_raw_plugin_constructed;
}
