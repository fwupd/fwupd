/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-plugin-vfuncs.h"

#include "fu-rts54hub-device.h"
#include "fu-rts54hub-rtd21xx-device.h"

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_plugin_set_device_gtype (plugin, FU_TYPE_RTS54HUB_DEVICE);
	fu_plugin_add_possible_quirk_key (plugin, "Rts54TargetAddr");
	fu_plugin_add_possible_quirk_key (plugin, "Rts54I2cSpeed");
	fu_plugin_add_possible_quirk_key (plugin, "Rts54RegisterAddrLen");
	g_type_ensure (FU_TYPE_RTS54HUB_RTD21XX_DEVICE);
}
