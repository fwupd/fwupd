/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-nitrokey-device.h"
#include "fu-nitrokey-plugin.h"

struct _FuNitrokeyPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuNitrokeyPlugin, fu_nitrokey_plugin, FU_TYPE_PLUGIN)

static void
fu_nitrokey_plugin_init(FuNitrokeyPlugin *self)
{
}

static void
fu_nitrokey_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_NITROKEY_DEVICE);
}

static void
fu_nitrokey_plugin_class_init(FuNitrokeyPluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->constructed = fu_nitrokey_plugin_constructed;
}
