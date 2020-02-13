/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-plugin-vfuncs.h"
#include "fu-hash.h"

#include "fu-ata-device.h"

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_plugin_add_udev_subsystem (plugin, "block");
	fu_plugin_set_device_gtype (plugin, FU_TYPE_ATA_DEVICE);
}

gboolean
fu_plugin_device_created (FuPlugin *plugin, FuDevice *dev, GError **error)
{
	gboolean tmp = fu_plugin_get_config_value_boolean (plugin, "UnknownOuiReport");
	fu_ata_device_set_unknown_oui_report (FU_ATA_DEVICE (dev), tmp);
	return TRUE;
}
