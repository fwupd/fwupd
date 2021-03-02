/*
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-plugin-vfuncs.h"

#include "fu-dfu-device.h"

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_plugin_set_device_gtype (plugin, FU_TYPE_DFU_DEVICE);
	fu_plugin_add_possible_quirk_key (plugin, "DfuAltName");
	fu_plugin_add_possible_quirk_key (plugin, "DfuForceTimeout");
	fu_plugin_add_possible_quirk_key (plugin, "DfuForceVersion");
}
