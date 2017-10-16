/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015-2016 Richard Hughes <richard@hughsie.com>
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

#define FU_PLUGIN_CHUG_POLL_REOPEN		5		/* seconds */
#define FU_PLUGIN_CHUG_FIRMWARE_MAX		(64 * 1024)	/* bytes */

struct FuPluginData {
	GHashTable		*devices;	/* DeviceKey:FuPluginItem */
	ChDeviceQueue		*device_queue;
};

typedef struct {
	FuDevice		*device;
	FuPlugin		*plugin;
	GUsbDevice		*usb_device;
	gboolean		 got_version;
	gboolean		 is_bootloader;
	guint			 timeout_open_id;
	GBytes			*fw_bin;
} FuPluginItem;

static gchar *
fu_plugin_colorhug_get_device_key (GUsbDevice *device)
{
	return g_strdup_printf ("%s_%s",
				g_usb_device_get_platform_id (device),
				ch_device_get_guid (device));
}

static void
fu_plugin_colorhug_item_free (FuPluginItem *item)
{
	g_object_unref (item->device);
	g_object_unref (item->plugin);
	g_object_unref (item->usb_device);
	if (item->fw_bin != NULL)
		g_bytes_unref (item->fw_bin);
	if (item->timeout_open_id != 0)
		g_source_remove (item->timeout_open_id);
}

static gboolean
fu_plugin_colorhug_wait_for_connect (FuPlugin *plugin,
				   FuPluginItem *item,
				   GError **error)
{
	GUsbContext *usb_ctx = fu_plugin_get_usb_context (plugin);
	g_autoptr(GUsbDevice) device = NULL;

	device = g_usb_context_wait_for_replug (usb_ctx,
						item->usb_device,
						CH_DEVICE_USB_TIMEOUT,
						error);
	if (device == NULL)
		return FALSE;

	/* update item */
	g_set_object (&item->usb_device, device);
	return TRUE;
}


static gboolean
fu_plugin_colorhug_open (FuPluginItem *item, GError **error)
{
	g_autoptr(GError) error_local = NULL;
	if (!ch_device_open (item->usb_device, &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_READ,
			     "failed to open %s device: %s",
			     fu_device_get_id (item->device),
			     error_local->message);
		return FALSE;
	}
	return TRUE;
}

static void
fu_plugin_colorhug_get_firmware_version (FuPluginItem *item)
{
	FuPluginData *data = fu_plugin_get_data (item->plugin);
	guint16 major;
	guint16 micro;
	guint16 minor;
	guint8 idx;
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *version = NULL;

	/* try to get the version without claiming interface */
	locker = fu_device_locker_new (item->usb_device, &error);
	if (locker == NULL) {
		g_debug ("Failed to open, polling: %s", error->message);
		return;
	}
	idx = g_usb_device_get_custom_index (item->usb_device,
					     G_USB_DEVICE_CLASS_VENDOR_SPECIFIC,
					     'F', 'W', NULL);
	if (idx != 0x00) {
		g_autofree gchar *tmp = NULL;
		tmp = g_usb_device_get_string_descriptor (item->usb_device,
							  idx, NULL);
		if (tmp != NULL) {
			item->got_version = TRUE;
			g_debug ("obtained fwver using extension '%s'", tmp);
			fu_device_set_version (item->device, tmp);
			return;
		}
	}

	/* attempt to open the device and get the serial number */
	if (!ch_device_open (item->usb_device, &error)) {
		g_debug ("Failed to claim interface, polling: %s", error->message);
		return;
	}
	ch_device_queue_get_firmware_ver (data->device_queue, item->usb_device,
					  &major, &minor, &micro);
	if (!ch_device_queue_process (data->device_queue,
				      CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				      NULL, &error)) {
		g_warning ("Failed to get serial: %s", error->message);
		return;
	}

	/* got things the old fashioned way */
	item->got_version = TRUE;
	version = g_strdup_printf ("%i.%i.%i", major, minor, micro);
	g_debug ("obtained fwver using API '%s'", version);
	fu_device_set_version (item->device, version);
}

gboolean
fu_plugin_verify (FuPlugin *plugin,
		  FuDevice *device,
		  FuPluginVerifyFlags flags,
		  GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	FuPluginItem *item;
	gsize len;
	g_autoptr(GError) error_local = NULL;
	g_autofree guint8 *data2 = NULL;
	GChecksumType checksum_types[] = {
		G_CHECKSUM_SHA1,
		G_CHECKSUM_SHA256,
		0 };

	/* find item */
	item = g_hash_table_lookup (data->devices, fu_device_get_id (device));
	if (item == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_FOUND,
			     "cannot find: %s",
			     fu_device_get_id (device));
		return FALSE;
	}

	/* open */
	if (!fu_plugin_colorhug_open (item, error))
		return FALSE;

	/* get the firmware from the device */
	g_debug ("ColorHug: Verifying firmware");
	ch_device_queue_read_firmware (data->device_queue, item->usb_device,
				       &data2, &len);
	fu_plugin_set_status (plugin, FWUPD_STATUS_DEVICE_VERIFY);
	if (!ch_device_queue_process (data->device_queue,
				      CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				      NULL, &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_WRITE,
			     "failed to dump firmware: %s",
			     error_local->message);
		g_usb_device_close (item->usb_device, NULL);
		return FALSE;
	}

	/* get the checksum */
	for (guint i = 0; checksum_types[i] != 0; i++) {
		g_autofree gchar *hash = NULL;
		hash = g_compute_checksum_for_data (checksum_types[i],
						    (guchar *) data2, len);
		fu_device_add_checksum (device, hash);
	}

	/* we're done here */
	if (!g_usb_device_close (item->usb_device, &error_local))
		g_debug ("Failed to close: %s", error_local->message);

	return TRUE;
}

gboolean
fu_plugin_update (FuPlugin *plugin,
		  FuDevice *device,
		  GBytes *blob_fw,
		  FwupdInstallFlags flags,
		  GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	FuPluginItem *item;
	g_autoptr(GError) error_local = NULL;

	/* find item */
	item = g_hash_table_lookup (data->devices,
				    fu_device_get_id (device));
	if (item == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_FOUND,
			     "cannot find: %s",
			     fu_device_get_id (device));
		return FALSE;
	}

	/* this file is so small, just slurp it all in one go */
	item->fw_bin = g_bytes_ref (blob_fw);

	/* check this firmware is actually for this device */
	if (!ch_device_check_firmware (item->usb_device,
				       g_bytes_get_data (item->fw_bin, NULL),
				       g_bytes_get_size (item->fw_bin),
				       &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "firmware is not suitable: %s",
			     error_local->message);
		return FALSE;
	}

	/* switch to bootloader mode */
	if (!item->is_bootloader) {
		g_debug ("ColorHug: Switching to bootloader mode");
		if (!fu_plugin_colorhug_open (item, error))
			return FALSE;
		ch_device_queue_reset (data->device_queue, item->usb_device);
		fu_plugin_set_status (plugin, FWUPD_STATUS_DEVICE_RESTART);
		if (!ch_device_queue_process (data->device_queue,
					      CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
					      NULL, &error_local)) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_WRITE,
				     "failed to reset device: %s",
				     error_local->message);
			g_usb_device_close (item->usb_device, NULL);
			return FALSE;
		}

		/* this device has just gone away, no error possible */
		g_usb_device_close (item->usb_device, NULL);

		/* wait for reconnection */
		g_debug ("ColorHug: Waiting for bootloader");
		if (!fu_plugin_colorhug_wait_for_connect (plugin, item, error))
			return FALSE;
	}

	/* open the device, which is now in bootloader mode */
	if (!fu_plugin_colorhug_open (item, error))
		return FALSE;

	/* write firmware */
	g_debug ("ColorHug: Writing firmware");
	ch_device_queue_write_firmware (data->device_queue, item->usb_device,
					g_bytes_get_data (item->fw_bin, NULL),
					g_bytes_get_size (item->fw_bin));
	fu_plugin_set_status (plugin, FWUPD_STATUS_DEVICE_WRITE);
	if (!ch_device_queue_process (data->device_queue,
				      CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				      NULL, &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_WRITE,
			     "failed to write firmware: %s",
			     error_local->message);
		g_usb_device_close (item->usb_device, NULL);
		return FALSE;
	}

	/* verify firmware */
	g_debug ("ColorHug: Veifying firmware");
	ch_device_queue_verify_firmware (data->device_queue, item->usb_device,
					 g_bytes_get_data (item->fw_bin, NULL),
					 g_bytes_get_size (item->fw_bin));
	fu_plugin_set_status (plugin, FWUPD_STATUS_DEVICE_VERIFY);
	if (!ch_device_queue_process (data->device_queue,
				      CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				      NULL, &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_WRITE,
			     "failed to verify firmware: %s",
			     error_local->message);
		g_usb_device_close (item->usb_device, NULL);
		return FALSE;
	}

	/* boot into the new firmware */
	g_debug ("ColorHug: Booting new firmware");
	ch_device_queue_boot_flash (data->device_queue, item->usb_device);
	fu_plugin_set_status (plugin, FWUPD_STATUS_DEVICE_RESTART);
	if (!ch_device_queue_process (data->device_queue,
				      CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				      NULL, &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_WRITE,
			     "failed to boot flash: %s",
			     error_local->message);
		g_usb_device_close (item->usb_device, NULL);
		return FALSE;
	}

	/* this device has just gone away, no error possible */
	g_usb_device_close (item->usb_device, NULL);

	/* wait for firmware mode */
	if (!fu_plugin_colorhug_wait_for_connect (plugin, item, error))
		return FALSE;
	if (!fu_plugin_colorhug_open (item, error))
		return FALSE;

	/* set flash success */
	g_debug ("ColorHug: Setting flash success");
	ch_device_queue_set_flash_success (data->device_queue, item->usb_device, 1);
	if (!ch_device_queue_process (data->device_queue,
				      CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				      NULL, &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_WRITE,
			     "failed to set flash success: %s",
			     error_local->message);
		g_usb_device_close (item->usb_device, NULL);
		return FALSE;
	}

	/* close, orderly */
	if (!g_usb_device_close (item->usb_device, &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_WRITE,
			     "failed to close device: %s",
			     error_local->message);
		g_usb_device_close (item->usb_device, NULL);
		return FALSE;
	}

	/* get the new firmware version */
	g_debug ("ColorHug: Getting new firmware version");
	item->got_version = FALSE;
	fu_plugin_colorhug_get_firmware_version (item);

	if (item->got_version)
		g_debug ("ColorHug: DONE!");

	return TRUE;
}

static gboolean
fu_plugin_colorhug_open_cb (gpointer user_data)
{
	FuPluginItem *item = (FuPluginItem *) user_data;

	g_debug ("attempt to open %s",
		 g_usb_device_get_platform_id (item->usb_device));
	fu_plugin_colorhug_get_firmware_version (item);

	/* success! */
	if (item->got_version) {
		item->timeout_open_id = 0;
		return FALSE;
	}

	/* keep trying */
	return TRUE;
}

static void
fu_plugin_colorhug_device_added_cb (GUsbContext *ctx,
				    GUsbDevice *device,
				    FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	FuPluginItem *item;
	ChDeviceMode mode;
	g_autofree gchar *device_key = NULL;

	/* ignore */
	mode = ch_device_get_mode (device);
	if (mode == CH_DEVICE_MODE_UNKNOWN)
		return;

	/* this is using DFU now */
	if (mode == CH_DEVICE_MODE_BOOTLOADER_PLUS ||
	    mode == CH_DEVICE_MODE_FIRMWARE_PLUS)
		return;

	/* is already in database */
	device_key = fu_plugin_colorhug_get_device_key (device);
	item = g_hash_table_lookup (data->devices, device_key);
	if (item == NULL) {
		item = g_new0 (FuPluginItem, 1);
		item->plugin = g_object_ref (plugin);
		item->usb_device = g_object_ref (device);
		item->device = fu_device_new ();
		fu_device_set_id (item->device, device_key);
		fu_device_set_vendor (item->device, "Hughski Limited");
		fu_device_set_vendor_id (item->device, "USB:0x273F");
		fu_device_set_equivalent_id (item->device,
					     g_usb_device_get_platform_id (device));
		fu_device_add_guid (item->device, ch_device_get_guid (device));
		fu_device_add_icon (item->device, "colorimeter-colorhug");
		fu_device_add_flag (item->device, FWUPD_DEVICE_FLAG_UPDATABLE);

		/* try to get the serial number -- if opening failed then
		 * poll until the device is not busy */
		fu_plugin_colorhug_get_firmware_version (item);
		if (!item->got_version && item->timeout_open_id == 0) {
			item->timeout_open_id = g_timeout_add_seconds (FU_PLUGIN_CHUG_POLL_REOPEN,
				fu_plugin_colorhug_open_cb, item);
		}

		/* insert to hash */
		g_hash_table_insert (data->devices, g_strdup (device_key), item);
	} else {
		/* update the device */
		g_object_unref (item->usb_device);
		item->usb_device = g_object_ref (device);
	}

	/* set the display name */
	switch (mode) {
	case CH_DEVICE_MODE_BOOTLOADER:
	case CH_DEVICE_MODE_FIRMWARE:
	case CH_DEVICE_MODE_LEGACY:
		fu_device_set_name (item->device, "ColorHug");
		fu_device_set_summary (item->device,
				       "An open source display colorimeter");
		break;
	case CH_DEVICE_MODE_BOOTLOADER2:
	case CH_DEVICE_MODE_FIRMWARE2:
		fu_device_set_name (item->device, "ColorHug2");
		fu_device_set_summary (item->device,
				       "An open source display colorimeter");
		break;
	case CH_DEVICE_MODE_BOOTLOADER_PLUS:
	case CH_DEVICE_MODE_FIRMWARE_PLUS:
		fu_device_set_name (item->device, "ColorHug+");
		fu_device_set_summary (item->device,
				       "An open source spectrophotometer");
		break;
	case CH_DEVICE_MODE_BOOTLOADER_ALS:
	case CH_DEVICE_MODE_FIRMWARE_ALS:
		fu_device_set_name (item->device, "ColorHugALS");
		fu_device_set_summary (item->device,
				       "An open source ambient light sensor");
		break;
	default:
		fu_device_set_name (item->device, "ColorHug??");
		break;
	}

	/* is the device in bootloader mode */
	switch (mode) {
	case CH_DEVICE_MODE_BOOTLOADER:
	case CH_DEVICE_MODE_BOOTLOADER2:
	case CH_DEVICE_MODE_BOOTLOADER_PLUS:
	case CH_DEVICE_MODE_BOOTLOADER_ALS:
		item->is_bootloader = TRUE;
		break;
	default:
		item->is_bootloader = FALSE;
		break;
	}
	fu_plugin_device_add (plugin, item->device);
}

static void
fu_plugin_colorhug_device_removed_cb (GUsbContext *ctx,
				      GUsbDevice *device,
				      FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	FuPluginItem *item;
	g_autofree gchar *device_key = NULL;

	/* already in database */
	device_key = fu_plugin_colorhug_get_device_key (device);
	item = g_hash_table_lookup (data->devices, device_key);
	if (item == NULL)
		return;

	/* no more polling for open */
	if (item->timeout_open_id != 0) {
		g_source_remove (item->timeout_open_id);
		item->timeout_open_id = 0;
	}
	fu_plugin_device_remove (plugin, item->device);
}

void
fu_plugin_init (FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_alloc_data (plugin, sizeof (FuPluginData));
	data->devices = g_hash_table_new_full (g_str_hash, g_str_equal,
					       g_free, (GDestroyNotify) fu_plugin_colorhug_item_free);
	data->device_queue = ch_device_queue_new ();
}

void
fu_plugin_destroy (FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	g_hash_table_unref (data->devices);
	g_object_unref (data->device_queue);
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
