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
	fu_plugin_set_device_gtype (plugin, DFU_TYPE_DEVICE);
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
