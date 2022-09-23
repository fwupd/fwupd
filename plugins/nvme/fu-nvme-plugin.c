/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-nvme-device.h"
#include "fu-nvme-plugin.h"

struct _FuNvmePlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuNvmePlugin, fu_nvme_plugin, FU_TYPE_PLUGIN)

static void
fu_nvme_plugin_init(FuNvmePlugin *self)
{
}

static void
fu_nvme_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_udev_subsystem(plugin, "nvme");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_NVME_DEVICE);
}

static void
fu_nvme_plugin_class_init(FuNvmePluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->constructed = fu_nvme_plugin_constructed;
}
