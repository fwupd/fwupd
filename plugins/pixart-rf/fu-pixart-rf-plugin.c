/*
 * Copyright 2020 Jimmy Yu <Jimmy_yu@pixart.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-pixart-rf-ble-device.h"
#include "fu-pixart-rf-firmware.h"
#include "fu-pixart-rf-plugin.h"
#include "fu-pixart-rf-receiver-device.h"
#include "fu-pixart-rf-wireless-device.h"

struct _FuPixartRfPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuPixartRfPlugin, fu_pixart_rf_plugin, FU_TYPE_PLUGIN)

static void
fu_pixart_rf_plugin_init(FuPixartRfPlugin *self)
{
}

static void
fu_pixart_rf_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_udev_subsystem(plugin, "hidraw");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_PIXART_RF_BLE_DEVICE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_PIXART_RF_RECEIVER_DEVICE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_PIXART_RF_WIRELESS_DEVICE); /* coverage */
	fu_plugin_add_firmware_gtype(plugin, "pixart", FU_TYPE_PIXART_RF_FIRMWARE);
}

static void
fu_pixart_rf_plugin_class_init(FuPixartRfPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_pixart_rf_plugin_constructed;
}
