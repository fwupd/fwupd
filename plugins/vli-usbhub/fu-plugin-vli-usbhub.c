/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-plugin-vfuncs.h"

#include "fu-vli-usbhub-device.h"
#include "fu-vli-usbhub-firmware.h"

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_plugin_add_rule (plugin, FU_PLUGIN_RULE_SUPPORTS_PROTOCOL, "com.vli.usbhub");
	fu_plugin_set_device_gtype (plugin, FU_TYPE_VLI_USBHUB_DEVICE);
	fu_plugin_add_firmware_gtype (plugin, "vli-usbhub", FU_TYPE_VLI_USBHUB_FIRMWARE);
}
