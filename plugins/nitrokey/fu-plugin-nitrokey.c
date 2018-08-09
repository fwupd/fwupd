/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-plugin.h"
#include "fu-plugin-vfuncs.h"

#include "fu-nitrokey-device.h"
#include "fu-nitrokey-common.h"

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_add_rule (plugin, FU_PLUGIN_RULE_REQUIRES_QUIRK, FU_QUIRKS_PLUGIN);
}

gboolean
fu_plugin_usb_device_added (FuPlugin *plugin, GUsbDevice *usb_device, GError **error)
{
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(FuNitrokeyDevice) device = NULL;

	/* open the device */
	device = fu_nitrokey_device_new (usb_device);
	locker = fu_device_locker_new (device, error);
	if (locker == NULL)
		return FALSE;

	/* success */
	fu_plugin_device_add (plugin, FU_DEVICE (device));
	return TRUE;
}
