/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-plugin-vfuncs.h"

#include "fu-synaptics-rmi-hid-device.h"
#include "fu-synaptics-rmi-ps2-device.h"
#include "fu-synaptics-rmi-firmware.h"

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_plugin_add_udev_subsystem (plugin, "hidraw");
	fu_plugin_add_udev_subsystem (plugin, "serio");
	g_type_ensure (FU_TYPE_SYNAPTICS_RMI_HID_DEVICE);
	g_type_ensure (FU_TYPE_SYNAPTICS_RMI_PS2_DEVICE);
	fu_plugin_add_firmware_gtype (plugin, NULL, FU_TYPE_SYNAPTICS_RMI_FIRMWARE);
}
