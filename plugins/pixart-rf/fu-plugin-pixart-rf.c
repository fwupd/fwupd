/*
 * Copyright (C) 2020 Jimmy Yu <Jimmy_yu@pixart.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-plugin-vfuncs.h"


#include "fu-pxi-device.h"
#include "fu-pxi-firmware.h"

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_plugin_add_udev_subsystem (plugin, "hidraw");
	fu_plugin_set_device_gtype (plugin, FU_TYPE_PXI_DEVICE);
	fu_plugin_add_firmware_gtype (plugin, "pixart", FU_TYPE_PXI_FIRMWARE);
}
