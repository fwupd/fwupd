/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-amt-device.h"
#include "fu-amt-plugin.h"

struct _FuAmtPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuAmtPlugin, fu_amt_plugin, FU_TYPE_PLUGIN)

static void
fu_amt_plugin_init(FuAmtPlugin *self)
{
}

static void
fu_amt_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_udev_subsystem(plugin, "mei");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_AMT_DEVICE);
}

static void
fu_amt_plugin_class_init(FuAmtPluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->constructed = fu_amt_plugin_constructed;
}
