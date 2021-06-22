/*
 * Copyright (C) 2020 Jimmy Yu <Jimmy_yu@pixart.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-pxi-ble-device.h"
#include "fu-pxi-receiver-device.h"
#include "fu-pxi-firmware.h"

void
fu_plugin_init (FuPlugin *plugin)
{
	FuContext *ctx = fu_plugin_get_context (plugin);
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_context_add_udev_subsystem (ctx, "hidraw");
	fu_plugin_add_device_gtype (plugin, FU_TYPE_PXI_BLE_DEVICE);
	fu_plugin_add_device_gtype (plugin, FU_TYPE_PXI_RECEIVER_DEVICE);
	fu_plugin_add_firmware_gtype (plugin, "pixart", FU_TYPE_PXI_FIRMWARE);
}
