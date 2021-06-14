/*
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-synaprom-device.h"
#include "fu-synaprom-firmware.h"

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_plugin_add_device_gtype (plugin, FU_TYPE_SYNAPROM_DEVICE);
	fu_plugin_add_firmware_gtype (plugin, NULL, FU_TYPE_SYNAPROM_FIRMWARE);
}
