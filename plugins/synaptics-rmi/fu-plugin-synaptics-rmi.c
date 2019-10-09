/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-plugin-vfuncs.h"

#include "fu-synaptics-rmi-device.h"

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_plugin_add_rule (plugin, FU_PLUGIN_RULE_SUPPORTS_PROTOCOL, "com.synaptics.rmi");
	fu_plugin_add_udev_subsystem (plugin, "hidraw");
	fu_plugin_set_device_gtype (plugin, FU_TYPE_SYNAPTICS_RMI_DEVICE);
}
