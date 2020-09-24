/*
 * Copyright (C) 2015-2017 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2020 boger wang <boger@goodix.com>
 * 
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"
#include "fu-plugin-vfuncs.h"
#include "fu-hash.h"
#include "fu-goodixmoc-device.h"

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_plugin_set_device_gtype (plugin, FU_TYPE_GOODIXMOC_DEVICE);
}
