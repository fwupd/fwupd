/*
 * Copyright (C) 2021 Xiaotian Cui <xtcui@analogixsemi.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-plugin-vfuncs.h"
#include "fu-hash.h"

#include "fu-anx-udev-device.h"
#include "fu-anx-udev-firmware.h"

#define MINIBONS_PARENT_GUID                "cfc5f783-2f3c-5db0-9d09-d5a3044eabd9"


void
fu_plugin_device_registered (FuPlugin *plugin, FuDevice *device)
{
	/* dfu plugin */
	if (g_strcmp0 (fu_device_get_plugin (device), "dfu") != 0 ||
		fu_device_has_flag (device, FWUPD_DEVICE_FLAG_INTERNAL))
		return;
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_UPDATABLE);
}

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_plugin_add_udev_subsystem (plugin, "anx-udev");
	fu_plugin_set_device_gtype (plugin, FU_TYPE_ANX_UDEV_DEVICE);
	fu_plugin_add_firmware_gtype (plugin, "anx-udev", FU_TYPE_ANX_UDEV_FIRMWARE);
	/*fu_plugin_add_rule (plugin, FU_PLUGIN_RULE_BETTER_THAN, "anx-udev_dock");*/
	g_debug ("anx-udev:fu_plugin_init");
}

