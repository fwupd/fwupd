/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-lenovo-dock-device.h"
#include "fu-lenovo-dock-firmware.h"
#include "fu-lenovo-dock-plugin.h"

struct _FuLenovoDockPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuLenovoDockPlugin, fu_lenovo_dock_plugin, FU_TYPE_PLUGIN)

static void
fu_lenovo_dock_plugin_init(FuLenovoDockPlugin *self)
{
	fu_plugin_add_flag(FU_PLUGIN(self), FWUPD_PLUGIN_FLAG_MUTABLE_ENUMERATION);
}

static void
fu_lenovo_dock_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_LENOVO_DOCK_DEVICE);
	fu_plugin_add_firmware_gtype(plugin, FU_TYPE_LENOVO_DOCK_FIRMWARE);
	fu_plugin_add_udev_subsystem(plugin, "usb");

	/* chain up to parent */
	G_OBJECT_CLASS(fu_lenovo_dock_plugin_parent_class)->constructed(obj);
}

static void
fu_lenovo_dock_plugin_class_init(FuLenovoDockPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_lenovo_dock_plugin_constructed;
}
