/*
 * Copyright 2026 Elan Microelectronics Corporation <paul.liang@emc.com.tw>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-elan-ts-firmware.h"
#include "fu-elan-ts-hid-device.h"
#include "fu-elan-ts-plugin.h"

struct _FuElanTsPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuElanTsPlugin, fu_elan_ts_plugin, FU_TYPE_PLUGIN)

static void
fu_elan_ts_plugin_init(FuElanTsPlugin *self)
{
}

static void
fu_elan_ts_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);

	/*
	 * Register only the hidraw subsystem.
	 * This plugin communicates with the touchscreen controller exclusively
	 * through the standard Linux hidraw interface.
	 */
	fu_plugin_add_udev_subsystem(plugin, "hidraw");
	fu_plugin_add_firmware_gtype(plugin, FU_TYPE_ELAN_TS_FIRMWARE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_ELAN_TS_HID_DEVICE);

	/* chain up to parent */
	G_OBJECT_CLASS(fu_elan_ts_plugin_parent_class)->constructed(obj);
}

static void
fu_elan_ts_plugin_class_init(FuElanTsPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_elan_ts_plugin_constructed;
}
