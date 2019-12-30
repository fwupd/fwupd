/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-hash.h"
#include "fu-plugin-vfuncs.h"

#include "fu-tpm-device.h"

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_plugin_add_udev_subsystem (plugin, "tpm");
	fu_plugin_set_device_gtype (plugin, FU_TYPE_TPM_DEVICE);
}
