/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-plugin-vfuncs.h"
#include "fu-hash.h"

#include "fu-elantp-firmware.h"
#include "fu-elantp-hid-device.h"

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_plugin_add_udev_subsystem (plugin, "hidraw");
	fu_plugin_add_firmware_gtype (plugin, "elantp", FU_TYPE_ELANTP_FIRMWARE);
	fu_plugin_set_device_gtype (plugin, FU_TYPE_ELANTP_HID_DEVICE);
}
