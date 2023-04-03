/*
 * Copyright (C) 2022 Haowei Lo <haowei.lo@fingerprints.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-fpc-device.h"
#include "fu-fpc-plugin.h"

struct _FuFpcPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuFpcPlugin, fu_fpc_plugin, FU_TYPE_PLUGIN)

static void
fu_fpc_plugin_init(FuFpcPlugin *self)
{
}

static void
fu_fpc_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_FPC_DEVICE);
}

static void
fu_fpc_plugin_class_init(FuFpcPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_fpc_constructed;
}
