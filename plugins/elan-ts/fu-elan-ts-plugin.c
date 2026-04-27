/*
 * Copyright 2026 Elan Microelectronics Corporation <paul.liang@emc.com.tw>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-elan-ts-hid-device.h"
#include "fu-elan-ts-firmware.h"
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

	//fu_plugin_add_udev_subsystem(plugin, "hidraw");
	/* 
	 * Register both hidraw and i2c subsystems. 
	 * Registering "i2c" allows the engine to match the physical I2C node 
	 * defined in the quirk file (e.g., [I2C\NAME_ELAN2703:00]).
	 */
	fu_plugin_add_udev_subsystem(plugin, "hidraw");
	fu_plugin_add_udev_subsystem(plugin, "i2c");

#if FWUPD_CHECK_VERSION(2, 1, 1)
	/* 
	 * In fwupd 2.1.1 and later, the fu_plugin_add_firmware_gtype 
	 * API was simplified by removing the 'id' parameter.
	 */
	fu_plugin_add_firmware_gtype(plugin, FU_TYPE_ELAN_TS_FIRMWARE);
#else
	/* 
	 * For older versions (e.g., 2.0.x), the API still requires 
	 * a NULL identifier as the second argument.
	 */
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_ELAN_TS_FIRMWARE);
#endif
	fu_plugin_add_device_gtype(plugin, FU_TYPE_ELAN_TS_HID_DEVICE);
}

static void
fu_elan_ts_plugin_class_init(FuElanTsPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_elan_ts_plugin_constructed;
}
