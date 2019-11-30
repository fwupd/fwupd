/*
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-ebitdo-device.h"

#include "fu-plugin-vfuncs.h"
#include "fu-hash.h"
#include "fu-ebitdo-firmware.h"

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_plugin_set_device_gtype (plugin, FU_TYPE_EBITDO_DEVICE);
	fu_plugin_add_firmware_gtype (plugin, "8bitdo", FU_TYPE_EBITDO_FIRMWARE);
}
