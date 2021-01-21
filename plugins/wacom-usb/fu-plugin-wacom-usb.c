/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-plugin-vfuncs.h"

#include "fu-wac-device.h"
#include "fu-wac-firmware.h"

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_plugin_set_device_gtype (plugin, FU_TYPE_WAC_DEVICE);
	fu_plugin_add_firmware_gtype (plugin, "wacom", FU_TYPE_WAC_FIRMWARE);
}

gboolean
fu_plugin_update (FuPlugin *plugin, FuDevice *device, GBytes *blob_fw,
		  FwupdInstallFlags flags, GError **error)
{
	FuDevice *parent = fu_device_get_parent (device);
	g_autoptr(FuDeviceLocker) locker = NULL;
	locker = fu_device_locker_new (parent != NULL ? parent : device, error);
	if (locker == NULL)
		return FALSE;
	return fu_device_write_firmware (device, blob_fw, flags, error);
}
