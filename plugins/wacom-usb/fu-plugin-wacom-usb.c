/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-plugin-vfuncs.h"
#include "fu-hash.h"

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

static FuDevice *
fu_plugin_wacom_usb_get_device (GPtrArray *devices)
{
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *dev = g_ptr_array_index (devices, i);
		if (FU_IS_WAC_DEVICE (dev))
			return dev;
	}
	return NULL;
}

gboolean
fu_plugin_composite_cleanup (FuPlugin *plugin,
			     GPtrArray *devices,
			     GError **error)
{
	FuDevice *device = fu_plugin_wacom_usb_get_device (devices);
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* not us */
	if (device == NULL)
		return TRUE;

	/* reboot, which switches the boot index of the firmware */
	locker = fu_device_locker_new (device, error);
	if (locker == NULL)
		return FALSE;
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_RESTART);
	return fu_wac_device_update_reset (FU_WAC_DEVICE (device), error);
}
