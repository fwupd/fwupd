/*
 * Copyright (C) 2020 Jimmy Yu <Jimmy_yu@pixart.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-pxi-ble-device.h"
#include "fu-pxi-firmware.h"
#include "fu-pxi-receiver-device.h"

static void
fu_plugin_pixart_init(FuPlugin *plugin)
{
	fu_plugin_add_udev_subsystem(plugin, "hidraw");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_PXI_BLE_DEVICE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_PXI_RECEIVER_DEVICE);
	fu_plugin_add_firmware_gtype(plugin, "pixart", FU_TYPE_PXI_FIRMWARE);
}

void
fu_plugin_init_vfuncs(FuPluginVfuncs *vfuncs)
{
	vfuncs->build_hash = FU_BUILD_HASH;
	vfuncs->init = fu_plugin_pixart_init;
}
