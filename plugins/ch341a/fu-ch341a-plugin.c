/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-ch341a-device.h"
#include "fu-ch341a-plugin.h"

struct _FuCh341APlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuCh341APlugin, fu_ch341a_plugin, FU_TYPE_PLUGIN)

static void
fu_ch341a_plugin_init(FuCh341APlugin *self)
{
}

static void
fu_ch341a_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_CH341A_DEVICE);
}

static void
fu_ch341a_plugin_class_init(FuCh341APluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->constructed = fu_ch341a_plugin_constructed;
}
