/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-ch347-device.h"
#include "fu-ch347-plugin.h"

struct _FuCh347Plugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuCh347Plugin, fu_ch347_plugin, FU_TYPE_PLUGIN)

static void
fu_ch347_plugin_init(FuCh347Plugin *self)
{
}

static void
fu_ch347_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_CH347_DEVICE);
}

static void
fu_ch347_plugin_class_init(FuCh347PluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_ch347_plugin_constructed;
}
