/*
 * Copyright (C) 2021
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-elanfp-device.h"
#include "fu-elanfp-firmware.h"
#include "fu-elanfp-plugin.h"

struct _FuElanfpPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuElanfpPlugin, fu_elanfp_plugin, FU_TYPE_PLUGIN)

static void
fu_elanfp_plugin_init(FuElanfpPlugin *self)
{
}

static void
fu_elanfp_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_ELANFP_DEVICE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_ELANFP_FIRMWARE);
}

static void
fu_elanfp_plugin_class_init(FuElanfpPluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->constructed = fu_elanfp_plugin_constructed;
}
