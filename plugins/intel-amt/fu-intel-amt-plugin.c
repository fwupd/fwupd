/*
 * Copyright 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-intel-amt-device.h"
#include "fu-intel-amt-plugin.h"

struct _FuIntelAmtPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuIntelAmtPlugin, fu_intel_amt_plugin, FU_TYPE_PLUGIN)

static void
fu_intel_amt_plugin_init(FuIntelAmtPlugin *self)
{
}

static void
fu_intel_amt_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_udev_subsystem(plugin, "mei");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_INTEL_AMT_DEVICE);
}

static void
fu_intel_amt_plugin_class_init(FuIntelAmtPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_intel_amt_plugin_constructed;
}
