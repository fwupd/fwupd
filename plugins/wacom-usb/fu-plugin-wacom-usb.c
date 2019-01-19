/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-plugin-vfuncs.h"

#include "fu-wac-device.h"

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_plugin_add_rule (plugin, FU_PLUGIN_RULE_REQUIRES_QUIRK, FU_QUIRKS_PLUGIN);
	fu_plugin_add_rule (plugin, FU_PLUGIN_RULE_SUPPORTS_PROTOCOL, "com.wacom.usb");
}

gboolean
fu_plugin_usb_device_added (FuPlugin *plugin, FuUsbDevice *device, GError **error)
{
	g_autoptr(FuWacDevice) dev = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;
	dev = fu_wac_device_new (device);
	locker = fu_device_locker_new (dev, error);
	if (locker == NULL)
		return FALSE;
	fu_plugin_device_add (plugin, FU_DEVICE (dev));
	return TRUE;
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
	return fu_device_write_firmware (device, blob_fw, error);
}
