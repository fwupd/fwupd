/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-hailuck-bl-device.h"
#include "fu-hailuck-kbd-device.h"
#include "fu-hailuck-kbd-firmware.h"

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_plugin_add_firmware_gtype (plugin, NULL, FU_TYPE_HAILUCK_KBD_FIRMWARE);
	fu_plugin_add_device_gtype (plugin, FU_TYPE_HAILUCK_BL_DEVICE);
	fu_plugin_add_device_gtype (plugin, FU_TYPE_HAILUCK_KBD_DEVICE);
}
