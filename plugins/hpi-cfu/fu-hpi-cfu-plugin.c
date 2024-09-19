/*
 * Copyright 2024 Pena Christian <christian.a.pena@hp.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-hpi-cfu-device.h"
#include "fu-hpi-cfu-plugin.h"

struct _FuHpiCfuPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuHpiCfuPlugin, fu_hpi_cfu_plugin, FU_TYPE_PLUGIN)

static void
fu_hpi_cfu_plugin_init(FuHpiCfuPlugin *self)
{
}

static void
fu_hpi_cfu_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_HPI_CFU_DEVICE);
}

static void
fu_hpi_cfu_plugin_class_init(FuHpiCfuPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_hpi_cfu_plugin_constructed;
}
