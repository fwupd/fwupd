/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-synaptics-rmi-firmware.h"
#include "fu-synaptics-rmi-hid-device.h"
#include "fu-synaptics-rmi-ps2-device.h"

static void
fu_plugin_synaptics_rmi_init(FuPlugin *plugin)
{
	fu_plugin_add_udev_subsystem(plugin, "hidraw");
	fu_plugin_add_udev_subsystem(plugin, "serio");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_SYNAPTICS_RMI_HID_DEVICE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_SYNAPTICS_RMI_PS2_DEVICE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_SYNAPTICS_RMI_FIRMWARE);
}

void
fu_plugin_init_vfuncs(FuPluginVfuncs *vfuncs)
{
	vfuncs->build_hash = FU_BUILD_HASH;
	vfuncs->init = fu_plugin_synaptics_rmi_init;
}
