/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2022 Kevin Chen <hsinfu.chen@qsitw.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-qsi-dock-mcu-device.h"
#include "fu-qsi-dock-plugin.h"

#define QSI_DOCK_TBT_INSTANCE_ID "THUNDERBOLT\\VEN_0108&DEV_2031"

struct _FuQsiDockPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuQsiDockPlugin, fu_qsi_dock_plugin, FU_TYPE_PLUGIN)

static void
fu_qsi_dock_plugin_mcu_registered(FuPlugin *plugin, FuDevice *device)
{
	/* usb device from thunderbolt plugin */
	if (g_strcmp0(fu_device_get_plugin(device), "thunderbolt") == 0 &&
	    fu_device_has_guid(device, QSI_DOCK_TBT_INSTANCE_ID)) {
		g_autofree gchar *msg = NULL;
		msg = g_strdup_printf("firmware update inhibited by [%s] plugin",
				      fu_plugin_get_name(plugin));
		fu_device_inhibit(device, "usb-blocked", msg);
	}
}

static void
fu_qsi_dock_plugin_init(FuQsiDockPlugin *self)
{
}

static void
fu_qsi_dock_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_QSI_DOCK_MCU_DEVICE);
	//fu_plugin_add_device_gtype(plugin, FU_TYPE_QSI_DOCK_DMC_DEVICE);
}

static void
fu_qsi_dock_plugin_class_init(FuQsiDockPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->constructed = fu_qsi_dock_plugin_constructed;
	plugin_class->device_registered = fu_qsi_dock_plugin_mcu_registered;
}
