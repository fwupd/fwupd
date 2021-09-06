/*
 * Copyright (C) 2021
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-elanfp-usb-device.h"

void
fu_plugin_init(FuPlugin *plugin)
{
	fu_plugin_set_build_hash(plugin, FU_BUILD_HASH);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_ELANFP_USB_DEVICE);
}
