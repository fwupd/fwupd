/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015-2017 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include <colord.h>
#include <colorhug.h>

#include "fu-plugin.h"
#include "fu-plugin-vfuncs.h"

#include "fu-colorhug-device.h"

gboolean
fu_plugin_update_detach (FuPlugin *plugin, FuDevice *device, GError **error)
{
	FuColorhugDevice *colorhug_dev = FU_COLORHUG_DEVICE (device);
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (device));
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(GUsbDevice) usb_device2 = NULL;

	/* open device */
	locker = fu_device_locker_new (device, error);
	if (locker == NULL)
		return FALSE;

	/* switch to bootloader mode is not required */
	if (fu_colorhug_device_get_is_bootloader (colorhug_dev)) {
		g_debug ("already in bootloader mode, skipping");
		return TRUE;
	}

	/* reset */
	fu_plugin_set_status (plugin, FWUPD_STATUS_DEVICE_RESTART);
	if (!fu_colorhug_device_detach (colorhug_dev, error))
		return FALSE;

	/* wait for replug */
	g_clear_object (&locker);
	usb_device2 = g_usb_context_wait_for_replug (fu_plugin_get_usb_context (plugin),
						     usb_device,
						     10000, error);
	if (usb_device2 == NULL) {
		g_prefix_error (error, "device did not come back: ");
		return FALSE;
	}

	/* set the new device until we can use a new FuDevice */
	fu_usb_device_set_dev (FU_USB_DEVICE (colorhug_dev), usb_device2);

	/* success */
	return TRUE;
}

gboolean
fu_plugin_update_attach (FuPlugin *plugin, FuDevice *device, GError **error)
{
	FuColorhugDevice *colorhug_dev = FU_COLORHUG_DEVICE (device);
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (device));
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(GUsbDevice) usb_device2 = NULL;

	/* open device */
	locker = fu_device_locker_new (device, error);
	if (locker == NULL)
		return FALSE;

	/* switch to runtime mode is not required */
	if (!fu_colorhug_device_get_is_bootloader (colorhug_dev)) {
		g_debug ("already in runtime mode, skipping");
		return TRUE;
	}

	/* reset */
	fu_plugin_set_status (plugin, FWUPD_STATUS_DEVICE_RESTART);
	if (!fu_colorhug_device_attach (colorhug_dev, error))
		return FALSE;

	/* wait for replug */
	g_clear_object (&locker);
	usb_device2 = g_usb_context_wait_for_replug (fu_plugin_get_usb_context (plugin),
						     usb_device,
						     10000, error);
	if (usb_device2 == NULL) {
		g_prefix_error (error, "device did not come back: ");
		return FALSE;
	}

	/* set the new device until we can use a new FuDevice */
	fu_usb_device_set_dev (FU_USB_DEVICE (colorhug_dev), usb_device2);

	/* success */
	return TRUE;
}

gboolean
fu_plugin_update_reload (FuPlugin *plugin, FuDevice *device, GError **error)
{
	FuColorhugDevice *colorhug_dev = FU_COLORHUG_DEVICE (device);
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* also set flash success */
	locker = fu_device_locker_new (device, error);
	if (locker == NULL)
		return FALSE;
	if (!fu_colorhug_device_set_flash_success (colorhug_dev, error))
		return FALSE;
	return TRUE;
}

static void
fu_plugin_colorhug_progress_cb (goffset current, goffset total, gpointer user_data)
{
	FuPlugin *plugin = FU_PLUGIN (user_data);
	fu_plugin_set_percentage (plugin, (guint) current);
}

gboolean
fu_plugin_update (FuPlugin *plugin,
		  FuDevice *device,
		  GBytes *blob_fw,
		  FwupdInstallFlags flags,
		  GError **error)
{
	FuColorhugDevice *colorhug_dev = FU_COLORHUG_DEVICE (device);
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (device));
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(GError) error_local = NULL;

	/* check this firmware is actually for this device */
	if (!ch_device_check_firmware (usb_device,
				       g_bytes_get_data (blob_fw, NULL),
				       g_bytes_get_size (blob_fw),
				       &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "firmware is not suitable: %s",
			     error_local->message);
		return FALSE;
	}

	/* write firmware */
	locker = fu_device_locker_new (device, error);
	if (locker == NULL)
		return FALSE;
	fu_plugin_set_status (plugin, FWUPD_STATUS_DEVICE_WRITE);
	return fu_colorhug_device_write_firmware (colorhug_dev, blob_fw,
						  fu_plugin_colorhug_progress_cb,
						  plugin,
						  error);
}

gboolean
fu_plugin_verify (FuPlugin *plugin,
		  FuDevice *device,
		  FuPluginVerifyFlags flags,
		  GError **error)
{
	FuColorhugDevice *colorhug_dev = FU_COLORHUG_DEVICE (device);
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* write firmware */
	locker = fu_device_locker_new (device, error);
	if (locker == NULL)
		return FALSE;
	fu_plugin_set_status (plugin, FWUPD_STATUS_DEVICE_VERIFY);
	return fu_colorhug_device_verify_firmware (colorhug_dev,
						   fu_plugin_colorhug_progress_cb,
						   plugin,
						   error);
}

static void
fu_plugin_colorhug_device_added_cb (GUsbContext *ctx,
				    GUsbDevice *usb_device,
				    FuPlugin *plugin)
{
	ChDeviceMode mode;
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(FuColorhugDevice) device = NULL;
	g_autoptr(GError) error = NULL;

	/* ignore */
	mode = ch_device_get_mode (usb_device);
	if (mode == CH_DEVICE_MODE_UNKNOWN)
		return;

	/* this is using DFU now */
	if (mode == CH_DEVICE_MODE_BOOTLOADER_PLUS ||
	    mode == CH_DEVICE_MODE_FIRMWARE_PLUS)
		return;

	/* open the device */
	device = fu_colorhug_device_new (usb_device);
	locker = fu_device_locker_new (device, &error);
	if (locker == NULL) {
		g_warning ("failed to open device: %s", error->message);
		return;
	}

	/* insert to hash */
	fu_plugin_device_add (plugin, FU_DEVICE (device));
	fu_plugin_cache_add (plugin, g_usb_device_get_platform_id (usb_device), device);
}

static void
fu_plugin_colorhug_device_removed_cb (GUsbContext *ctx,
				      GUsbDevice *usb_device,
				      FuPlugin *plugin)
{
	FuDevice *device;
	const gchar *platform_id = NULL;

	/* already in database */
	platform_id = g_usb_device_get_platform_id (usb_device);
	device = fu_plugin_cache_lookup (plugin, platform_id);
	if (device == NULL)
		return;

	fu_plugin_device_remove (plugin, device);
	fu_plugin_cache_remove (plugin, platform_id);
}

gboolean
fu_plugin_startup (FuPlugin *plugin, GError **error)
{
	GUsbContext *usb_ctx = fu_plugin_get_usb_context (plugin);
	g_signal_connect (usb_ctx, "device-added",
			  G_CALLBACK (fu_plugin_colorhug_device_added_cb),
			  plugin);
	g_signal_connect (usb_ctx, "device-removed",
			  G_CALLBACK (fu_plugin_colorhug_device_removed_cb),
			  plugin);
	return TRUE;
}
