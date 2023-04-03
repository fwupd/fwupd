/*
 * Copyright (C) 2020 Jimmy Yu <Jimmy_yu@pixart.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-pxi-ble-device.h"
#include "fu-pxi-firmware.h"
#include "fu-pxi-plugin.h"
#include "fu-pxi-receiver-device.h"

struct _FuPxiPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuPxiPlugin, fu_pxi_plugin, FU_TYPE_PLUGIN)

static void
fu_pxi_plugin_init(FuPxiPlugin *self)
{
}

static void
fu_pxi_plugin_object_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_set_name(plugin, "pixart_rf");
}

static void
fu_pxi_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_udev_subsystem(plugin, "hidraw");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_PXI_BLE_DEVICE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_PXI_RECEIVER_DEVICE);
	fu_plugin_add_firmware_gtype(plugin, "pixart", FU_TYPE_PXI_FIRMWARE);
}

static void
fu_pxi_plugin_class_init(FuPxiPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->constructed = fu_pxi_plugin_object_constructed;
	plugin_class->constructed = fu_pxi_plugin_constructed;
}
