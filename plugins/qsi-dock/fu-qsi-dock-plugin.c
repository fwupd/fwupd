/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2022 Kevin Chen <hsinfu.chen@qsitw.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-qsi-dock-mcu-device.h"
#include "fu-qsi-dock-plugin.h"

struct _FuQsiDockPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuQsiDockPlugin, fu_qsi_dock_plugin, FU_TYPE_PLUGIN)

static void
fu_qsi_dock_plugin_init(FuQsiDockPlugin *self)
{
}

static void
fu_qsi_dock_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_QSI_DOCK_MCU_DEVICE);
}

static void
fu_qsi_dock_plugin_class_init(FuQsiDockPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_qsi_dock_plugin_constructed;
}
