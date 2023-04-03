/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-dfu-csr-device.h"
#include "fu-dfu-csr-plugin.h"

struct _FuDfuCsrPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuDfuCsrPlugin, fu_dfu_csr_plugin, FU_TYPE_PLUGIN)

static void
fu_dfu_csr_plugin_init(FuDfuCsrPlugin *self)
{
}

static void
fu_dfu_csr_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_DFU_CSR_DEVICE);
}

static void
fu_dfu_csr_plugin_class_init(FuDfuCsrPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_dfu_csr_plugin_constructed;
}
