/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-plugin.h"
#include "fu-plugin-vfuncs.h"

#include "fu-nitrokey-device.h"
#include "fu-nitrokey-common.h"

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
