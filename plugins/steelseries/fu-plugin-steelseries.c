/*
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2022 Gaël PORTAY <gael.portay@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-steelseries-fizz-hid.h"
#include "fu-steelseries-fizz-tunnel.h"
#include "fu-steelseries-fizz.h"
#include "fu-steelseries-gamepad.h"
#include "fu-steelseries-mouse.h"
#include "fu-steelseries-sonic.h"

static void
fu_plugin_steelseries_init(FuPlugin *plugin)
{
	fu_plugin_add_device_gtype(plugin, FU_TYPE_STEELSERIES_FIZZ);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_STEELSERIES_FIZZ_HID);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_STEELSERIES_FIZZ_TUNNEL);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_STEELSERIES_GAMEPAD);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_STEELSERIES_MOUSE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_STEELSERIES_SONIC);
	fu_plugin_add_udev_subsystem(plugin, "hidraw");
}

void
fu_plugin_init_vfuncs(FuPluginVfuncs *vfuncs)
{
	vfuncs->build_hash = FU_BUILD_HASH;
	vfuncs->init = fu_plugin_steelseries_init;
}
