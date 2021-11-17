/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-lenovo-dock-dmc-device.h"
#include "fu-lenovo-dock-firmware.h"
#include "fu-lenovo-dock-mcu-device.h"

void
fu_plugin_init(FuPlugin *plugin)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	fu_plugin_set_build_hash(plugin, FU_BUILD_HASH);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_LENOVO_DOCK_MCU_DEVICE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_LENOVO_DOCK_DMC_DEVICE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_LENOVO_DOCK_FIRMWARE);
	fu_context_add_quirk_key(ctx, "LenovoDockVersionFormat");
}
