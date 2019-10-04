/*
 * Copyright (C) 2015-2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-plugin-vfuncs.h"

#include "fu-colorhug-device.h"

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_plugin_add_rule (plugin, FU_PLUGIN_RULE_REQUIRES_QUIRK, FU_QUIRKS_PLUGIN);
	fu_plugin_add_rule (plugin, FU_PLUGIN_RULE_SUPPORTS_PROTOCOL, "com.hughski.colorhug");
}

gboolean
fu_plugin_usb_device_added (FuPlugin *plugin, FuUsbDevice *device, GError **error)
{
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(FuColorhugDevice) dev = NULL;

	/* open the device */
	dev = fu_colorhug_device_new (device);
	locker = fu_device_locker_new (dev, error);
	if (locker == NULL)
		return FALSE;

	/* insert to hash */
	fu_plugin_device_add (plugin, FU_DEVICE (dev));
	return TRUE;
}
