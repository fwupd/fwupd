/*
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-dfu-device.h"
#include "fu-dfu-plugin.h"

struct _FuDfuPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuDfuPlugin, fu_dfu_plugin, FU_TYPE_PLUGIN)

static void
fu_dfu_plugin_init(FuDfuPlugin *self)
{
}

static void
fu_dfu_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	FuContext *ctx = fu_plugin_get_context(plugin);
	fu_context_add_quirk_key(ctx, "DfuAltName");
	fu_context_add_quirk_key(ctx, "DfuForceTimeout");
	fu_context_add_quirk_key(ctx, "DfuForceVersion");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_DFU_DEVICE);
}

static void
fu_dfu_plugin_class_init(FuDfuPluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->constructed = fu_dfu_plugin_constructed;
}
