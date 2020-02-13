/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-plugin-vfuncs.h"
#include "fu-hash.h"

#include "fu-fresco-pd-device.h"
#include "fu-fresco-pd-firmware.h"

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_plugin_set_device_gtype (plugin, FU_TYPE_FRESCO_PD_DEVICE);
	fu_plugin_add_firmware_gtype (plugin, "fresco-pd", FU_TYPE_FRESCO_PD_FIRMWARE);
}
