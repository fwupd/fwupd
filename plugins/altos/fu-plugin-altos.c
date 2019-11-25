/*
 * Copyright (C) 2016-2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-plugin-vfuncs.h"
#include "fu-hash.h"

#include "fu-altos-device.h"
#include "fu-altos-firmware.h"

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_plugin_set_device_gtype (plugin, FU_TYPE_ALTOS_DEVICE);
	fu_plugin_add_firmware_gtype (plugin, "altos", FU_TYPE_ALTOS_FIRMWARE);
}
