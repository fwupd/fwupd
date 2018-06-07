/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <appstream-glib.h>

#include "fu-plugin.h"
#include "fu-plugin-vfuncs.h"

#include "dfu-device.h"

static void
fu_plugin_dfu_state_changed_cb (DfuDevice *device,
				DfuState state,
				FuPlugin *plugin)
{
	switch (state) {
	case DFU_STATE_DFU_UPLOAD_IDLE:
		fu_device_set_status (FU_DEVICE (device), FWUPD_STATUS_DEVICE_VERIFY);
		break;
	case DFU_STATE_DFU_DNLOAD_IDLE:
		fu_device_set_status (FU_DEVICE (device), FWUPD_STATUS_DEVICE_WRITE);
		break;
	default:
		break;
	}
}

gboolean
fu_plugin_usb_device_added (FuPlugin *plugin, GUsbDevice *usb_device, GError **error)
{
	g_autoptr(DfuDevice) device = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* open the device */
	device = dfu_device_new (usb_device);
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

	/* watch all signals */
	g_signal_connect (device, "state-changed",
			  G_CALLBACK (fu_plugin_dfu_state_changed_cb), plugin);

	/* this is a guess and can be overridden in the metainfo file */
	fu_device_add_icon (device, "drive-harddisk-usb");

	/* insert to hash */
	fu_plugin_device_add (plugin, FU_DEVICE (device));
	return TRUE;
}

gboolean
fu_plugin_update_detach (FuPlugin *plugin, FuDevice *dev, GError **error)
{
	DfuDevice *device = DFU_DEVICE (dev);
	g_autoptr(FuDeviceLocker) locker  = NULL;
	g_autoptr(GError) error_local = NULL;

	/* open it */
	locker = fu_device_locker_new (device, &error_local);
	if (locker == NULL)
		return FALSE;
	if (!dfu_device_refresh_and_clear (device, error))
		return FALSE;

	/* already in DFU mode */
	if (!dfu_device_is_runtime (device))
		return TRUE;

	/* detach and USB reset */
	if (!dfu_device_detach (device, error))
		return FALSE;
	if (!dfu_device_wait_for_replug (device, FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE, error))
		return FALSE;

	/* success */
	return TRUE;
}

gboolean
fu_plugin_update_attach (FuPlugin *plugin, FuDevice *dev, GError **error)
{
	DfuDevice *device = DFU_DEVICE (dev);
	g_autoptr(FuDeviceLocker) locker  = NULL;
	g_autoptr(GError) error_local = NULL;

	/* open it */
	locker = fu_device_locker_new (device, &error_local);
	if (locker == NULL)
		return FALSE;
	if (!dfu_device_refresh_and_clear (device, error))
		return FALSE;

	/* already in runtime mode */
	if (dfu_device_is_runtime (device))
		return TRUE;

	/* attach it */
	if (!dfu_device_attach (device, error))
		return FALSE;
	if (!dfu_device_wait_for_replug (device, FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE, error))
		return FALSE;

	/* success */
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

gboolean
fu_plugin_verify (FuPlugin *plugin,
		  FuDevice *dev,
		  FuPluginVerifyFlags flags,
		  GError **error)
{
	GBytes *blob_fw;
	DfuDevice *device = DFU_DEVICE (dev);
	g_autoptr(DfuFirmware) dfu_firmware = NULL;
	g_autoptr(FuDeviceLocker) locker  = NULL;
	g_autoptr(GError) error_local = NULL;
	GChecksumType checksum_types[] = {
		G_CHECKSUM_SHA1,
		G_CHECKSUM_SHA256,
		0 };

	/* open it */
	locker = fu_device_locker_new (device, &error_local);
	if (locker == NULL)
		return FALSE;
	if (!dfu_device_refresh_and_clear (device, error))
		return FALSE;

	/* get data from hardware */
	g_debug ("uploading from device->host");
	dfu_firmware = dfu_device_upload (device,
					  DFU_TARGET_TRANSFER_FLAG_NONE,
					  error);
	if (dfu_firmware == NULL)
		return FALSE;

	/* get the checksum */
	blob_fw = dfu_firmware_write_data (dfu_firmware, error);
	if (blob_fw == NULL)
		return FALSE;
	for (guint i = 0; checksum_types[i] != 0; i++) {
		g_autofree gchar *hash = NULL;
		hash = g_compute_checksum_for_bytes (checksum_types[i], blob_fw);
		fu_device_add_checksum (dev, hash);
	}

	/* success */
	return TRUE;
}
