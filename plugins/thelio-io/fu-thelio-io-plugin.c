/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2019 Jeremy Soller <jeremy@system76.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-thelio-io-device.h"
#include "fu-thelio-io-plugin.h"

struct _FuThelioIoPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuThelioIoPlugin, fu_thelio_io_plugin, FU_TYPE_PLUGIN)

static void
fu_thelio_io_plugin_init(FuThelioIoPlugin *self)
{
}

static void
fu_thelio_io_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_THELIO_IO_DEVICE);
}

static void
fu_thelio_io_plugin_class_init(FuThelioIoPluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->constructed = fu_thelio_io_plugin_constructed;
}
