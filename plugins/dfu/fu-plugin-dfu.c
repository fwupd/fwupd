/*
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-plugin-vfuncs.h"

#include "dfu-device.h"

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_plugin_add_rule (plugin, FU_PLUGIN_RULE_SUPPORTS_PROTOCOL, "org.usb.dfu");
	fu_plugin_add_rule (plugin, FU_PLUGIN_RULE_SUPPORTS_PROTOCOL, "com.st.dfuse");
}

gboolean
fu_plugin_usb_device_added (FuPlugin *plugin, FuUsbDevice *dev, GError **error)
{
	g_autoptr(DfuDevice) device = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* open the device */
	device = dfu_device_new (fu_usb_device_get_dev (dev));
	fu_device_set_quirks (FU_DEVICE (device), fu_plugin_get_quirks (plugin));
	dfu_device_set_usb_context (device, fu_plugin_get_usb_context (plugin));
	locker = fu_device_locker_new (device, error);
	if (locker == NULL)
		return FALSE;

	/* ignore defective runtimes */
	if (dfu_device_is_runtime (device) &&
	    dfu_device_has_quirk (device, DFU_DEVICE_QUIRK_IGNORE_RUNTIME)) {
		g_debug ("ignoring %s runtime", dfu_device_get_platform_id (device));
		return TRUE;
	}

	/* this is a guess and can be overridden in the metainfo file */
	fu_device_add_icon (device, "drive-harddisk-usb");

	/* insert to hash */
	fu_plugin_device_add (plugin, FU_DEVICE (device));
	return TRUE;
}

gboolean
fu_plugin_update (FuPlugin *plugin,
		  FuDevice *dev,
		  GBytes *blob_fw,
		  FwupdInstallFlags flags,
		  GError **error)
{
	DfuDevice *device = DFU_DEVICE (dev);
	g_autoptr(DfuFirmware) dfu_firmware = NULL;
	g_autoptr(FuDeviceLocker) locker  = NULL;
	g_autoptr(GError) error_local = NULL;

	/* open it */
	locker = fu_device_locker_new (device, &error_local);
	if (locker == NULL)
		return FALSE;
	if (!dfu_device_refresh_and_clear (device, error))
		return FALSE;

	/* hit hardware */
	dfu_firmware = dfu_firmware_new ();
	if (!dfu_firmware_parse_data (dfu_firmware, blob_fw,
				      DFU_FIRMWARE_PARSE_FLAG_NONE, error))
		return FALSE;
	if (!dfu_device_download (device, dfu_firmware,
				  DFU_TARGET_TRANSFER_FLAG_VERIFY |
				  DFU_TARGET_TRANSFER_FLAG_WILDCARD_VID |
				  DFU_TARGET_TRANSFER_FLAG_WILDCARD_PID,
				  error))
		return FALSE;

	/* success */
	return TRUE;
}
