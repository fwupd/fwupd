/*
 * Copyright (C) 2021 Synaptics Incorporated <simon.ho@synaptics.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-synaptics-cape-device.h"
#include "fu-synaptics-cape-firmware.h"

void
fu_plugin_init(FuPlugin *plugin)
{
	fu_plugin_set_build_hash(plugin, FU_BUILD_HASH);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_SYNAPTICS_CAPE_DEVICE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_SYNAPTICS_CAPE_FIRMWARE);
}
