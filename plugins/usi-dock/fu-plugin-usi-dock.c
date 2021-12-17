/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-usi-dock-dmc-device.h"
#include "fu-usi-dock-firmware.h"
#include "fu-usi-dock-mcu-device.h"

#define USI_DOCK_TBT_INSTANCE_ID "THUNDERBOLT\\VEN_0108&DEV_2031"

static void
fu_plugin_usi_dock_dmc_registered(FuPlugin *plugin, FuDevice *device)
{
	/* usb device from thunderbolt plugin */
	if (g_strcmp0(fu_device_get_plugin(device), "thunderbolt") == 0 &&
	    fu_device_has_guid(device, USI_DOCK_TBT_INSTANCE_ID)) {
		g_autofree gchar *msg = NULL;
		msg = g_strdup_printf("firmware update inhibited by [%s] plugin",
				      fu_plugin_get_name(plugin));
		fu_device_inhibit(device, "usb-blocked", msg);
	}
}

static void
fu_usi_dock_init(FuPlugin *plugin)
{
	fu_plugin_add_device_gtype(plugin, FU_TYPE_USI_DOCK_MCU_DEVICE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_USI_DOCK_DMC_DEVICE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_USI_DOCK_FIRMWARE);
}

void
fu_plugin_init_vfuncs(FuPluginVfuncs *vfuncs)
{
	vfuncs->build_hash = FU_BUILD_HASH;
	vfuncs->init = fu_usi_dock_init;
	vfuncs->device_registered = fu_plugin_usi_dock_dmc_registered;
}
