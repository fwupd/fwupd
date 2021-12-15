/*
 * Copyright (C) 2021 Ricardo Cañuelo <ricardo.canuelo@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-nordic-hid-archive.h"
#include "fu-nordic-hid-cfg-channel.h"
#include "fu-nordic-hid-firmware-b0.h"
#include "fu-nordic-hid-firmware-mcuboot.h"

static void
fu_plugin_nordic_hid_init(FuPlugin *plugin)
{
	FuContext *ctx = fu_plugin_get_context(plugin);

	fu_plugin_add_udev_subsystem(plugin, "hidraw");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_NORDIC_HID_CFG_CHANNEL);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_NORDIC_HID_ARCHIVE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_NORDIC_HID_FIRMWARE_B0);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_NORDIC_HID_FIRMWARE_MCUBOOT);
	fu_context_add_quirk_key(ctx, "NordicHidBootloader");
}

void
fu_plugin_init_vfuncs(FuPluginVfuncs *vfuncs)
{
	vfuncs->build_hash = FU_BUILD_HASH;
	vfuncs->init = fu_plugin_nordic_hid_init;
}
