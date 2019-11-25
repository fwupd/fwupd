/*
 * Copyright (C) 2018-2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-plugin-vfuncs.h"
#include "fu-hash.h"
#include "fu-wacom-aes-device.h"
#include "fu-wacom-emr-device.h"
#include "fu-wacom-common.h"

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_plugin_add_udev_subsystem (plugin, "hidraw");

	/* register the custom types */
	g_type_ensure (FU_TYPE_WACOM_AES_DEVICE);
	g_type_ensure (FU_TYPE_WACOM_EMR_DEVICE);
}
