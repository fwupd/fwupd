/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
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

#include <fwupd.h>
#include <colord.h>
#include <colorhug.h>
#include <gio/gio.h>
#include <gio/gunixinputstream.h>
#include <glib-object.h>
#include <gusb.h>

#include "fu-device.h"
#include "fu-provider-chug.h"

static void	fu_provider_chug_finalize	(GObject	*object);

#define FU_PROVIDER_CHUG_POLL_REOPEN		5		/* seconds */
#define FU_PROVIDER_CHUG_FIRMWARE_MAX		(64 * 1024)	/* bytes */

/**
 * FuProviderChugPrivate:
 **/
typedef struct {
	GHashTable		*devices;	/* DeviceKey:FuProviderChugItem */
	GUsbContext		*usb_ctx;
	ChDeviceQueue		*device_queue;
} FuProviderChugPrivate;

typedef struct {
	FuDevice		*device;
	FuProviderChug		*provider_chug;
	GMainLoop		*loop;
	GUsbDevice		*usb_device;
	gboolean		 got_version;
	gboolean		 is_bootloader;
	guint			 timeout_open_id;
	guint			 reconnect_id;
	GBytes			*fw_bin;
} FuProviderChugItem;

G_DEFINE_TYPE_WITH_PRIVATE (FuProviderChug, fu_provider_chug, FU_TYPE_PROVIDER)
#define GET_PRIVATE(o) (fu_provider_chug_get_instance_private (o))

/**
 * fu_provider_chug_get_name:
 **/
static const gchar *
fu_provider_chug_get_name (FuProvider *provider)
{
	return "ColorHug";
}

/**
 * fu_provider_chug_get_device_key:
 **/
static gchar *
fu_provider_chug_get_device_key (GUsbDevice *device)
{
	return g_strdup_printf ("%s_%s",
				g_usb_device_get_platform_id (device),
				ch_device_get_guid (device));
}

/**
 * fu_provider_chug_device_free:
 **/
static void
fu_provider_chug_device_free (FuProviderChugItem *item)
{
	g_main_loop_unref (item->loop);
	g_object_unref (item->device);
	g_object_unref (item->provider_chug);
	g_object_unref (item->usb_device);
	if (item->fw_bin != NULL)
		g_bytes_unref (item->fw_bin);
	if (item->timeout_open_id != 0)
		g_source_remove (item->timeout_open_id);
	if (item->reconnect_id != 0)
		g_source_remove (item->reconnect_id);
}

/**
 * fu_provider_chug_reconnect_timeout_cb:
 **/
static gboolean
fu_provider_chug_reconnect_timeout_cb (gpointer user_data)
{
	FuProviderChugItem *item = (FuProviderChugItem *) user_data;
	item->reconnect_id = 0;
	g_main_loop_quit (item->loop);
	return FALSE;
}

/**
 * fu_provider_chug_wait_for_connect:
 **/
static gboolean
fu_provider_chug_wait_for_connect (FuProviderChugItem *item, GError **error)
{
	item->reconnect_id = g_timeout_add (CH_DEVICE_USB_TIMEOUT,
				fu_provider_chug_reconnect_timeout_cb, item);
	g_main_loop_run (item->loop);
	if (item->reconnect_id == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_FOUND,
				     "request timed out");
		return FALSE;
	}
	g_source_remove (item->reconnect_id);
	item->reconnect_id = 0;
	return TRUE;
}


/**
 * fu_provider_chug_open:
 **/
static gboolean
fu_provider_chug_open (FuProviderChugItem *item, GError **error)
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

/**
 * fu_provider_chug_get_firmware_version:
 **/
static void
fu_provider_chug_get_firmware_version (FuProviderChugItem *item)
{
	FuProviderChugPrivate *priv = GET_PRIVATE (item->provider_chug);
	guint16 major;
	guint16 micro;
	guint16 minor;
	guint8 idx;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *version = NULL;

	/* try to get the version without claiming interface */
	if (!g_usb_device_open (item->usb_device, &error)) {
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
			fu_device_set_metadata (item->device,
						FU_DEVICE_KEY_VERSION, tmp);
			goto out;
		}
	}
	g_usb_device_close (item->usb_device, NULL);

	/* attempt to open the device and get the serial number */
	if (!ch_device_open (item->usb_device, &error)) {
		g_debug ("Failed to claim interface, polling: %s", error->message);
		return;
	}
	ch_device_queue_get_firmware_ver (priv->device_queue, item->usb_device,
					  &major, &minor, &micro);
	if (!ch_device_queue_process (priv->device_queue,
				      CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				      NULL, &error)) {
		g_warning ("Failed to get serial: %s", error->message);
		goto out;
	}

	/* got things the old fashioned way */
	item->got_version = TRUE;
	version = g_strdup_printf ("%i.%i.%i", major, minor, micro);
	g_debug ("obtained fwver using API '%s'", version);
	fu_device_set_metadata (item->device, FU_DEVICE_KEY_VERSION, version);

out:
	/* we're done here */
	g_clear_error (&error);
	if (!g_usb_device_close (item->usb_device, &error))
		g_debug ("Failed to close: %s", error->message);
}

/**
 * fu_provider_chug_verify:
 **/
static gboolean
fu_provider_chug_verify (FuProvider *provider,
			 FuDevice *device,
			 FuProviderVerifyFlags flags,
			 GError **error)
{
	FuProviderChug *provider_chug = FU_PROVIDER_CHUG (provider);
	FuProviderChugPrivate *priv = GET_PRIVATE (provider_chug);
	FuProviderChugItem *item;
	gsize len;
	g_autoptr(GError) error_local = NULL;
	g_autofree gchar *hash = NULL;
	g_autofree guint8 *data = NULL;

	/* find item */
	item = g_hash_table_lookup (priv->devices, fu_device_get_id (device));
	if (item == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_FOUND,
			     "cannot find: %s",
			     fu_device_get_id (device));
		return FALSE;
	}

	/* open */
	if (!fu_provider_chug_open (item, error))
		return FALSE;

	/* get the firmware from the device */
	g_debug ("ColorHug: Verifying firmware");
	ch_device_queue_read_firmware (priv->device_queue, item->usb_device,
				       &data, &len);
	fu_provider_set_status (provider, FWUPD_STATUS_DEVICE_VERIFY);
	if (!ch_device_queue_process (priv->device_queue,
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

	/* get the SHA1 hash */
	hash = g_compute_checksum_for_data (G_CHECKSUM_SHA1, (guchar *) data, len);
	fu_device_set_metadata (device, FU_DEVICE_KEY_FIRMWARE_HASH, hash);

	/* we're done here */
	if (!g_usb_device_close (item->usb_device, &error_local))
		g_debug ("Failed to close: %s", error_local->message);

	return TRUE;
}

/**
 * fu_provider_chug_update:
 **/
static gboolean
fu_provider_chug_update (FuProvider *provider,
			 FuDevice *device,
			 GBytes *blob_fw,
			 FuProviderFlags flags,
			 GError **error)
{
	FuProviderChug *provider_chug = FU_PROVIDER_CHUG (provider);
	FuProviderChugPrivate *priv = GET_PRIVATE (provider_chug);
	FuProviderChugItem *item;
	g_autoptr(GError) error_local = NULL;

	/* find item */
	item = g_hash_table_lookup (priv->devices,
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
		if (!fu_provider_chug_open (item, error))
			return FALSE;
		ch_device_queue_reset (priv->device_queue, item->usb_device);
		fu_provider_set_status (provider, FWUPD_STATUS_DEVICE_RESTART);
		if (!ch_device_queue_process (priv->device_queue,
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
		if (!fu_provider_chug_wait_for_connect (item, error))
			return FALSE;
	}

	/* open the device, which is now in bootloader mode */
	if (!fu_provider_chug_open (item, error))
		return FALSE;

	/* write firmware */
	g_debug ("ColorHug: Writing firmware");
	ch_device_queue_write_firmware (priv->device_queue, item->usb_device,
					g_bytes_get_data (item->fw_bin, NULL),
					g_bytes_get_size (item->fw_bin));
	fu_provider_set_status (provider, FWUPD_STATUS_DEVICE_WRITE);
	if (!ch_device_queue_process (priv->device_queue,
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
	ch_device_queue_verify_firmware (priv->device_queue, item->usb_device,
					 g_bytes_get_data (item->fw_bin, NULL),
					 g_bytes_get_size (item->fw_bin));
	fu_provider_set_status (provider, FWUPD_STATUS_DEVICE_VERIFY);
	if (!ch_device_queue_process (priv->device_queue,
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
	ch_device_queue_boot_flash (priv->device_queue, item->usb_device);
	fu_provider_set_status (provider, FWUPD_STATUS_DEVICE_RESTART);
	if (!ch_device_queue_process (priv->device_queue,
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
	if (!fu_provider_chug_wait_for_connect (item, error))
		return FALSE;
	if (!fu_provider_chug_open (item, error))
		return FALSE;

	/* set flash success */
	g_debug ("ColorHug: Setting flash success");
	ch_device_queue_set_flash_success (priv->device_queue, item->usb_device, 1);
	if (!ch_device_queue_process (priv->device_queue,
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
	fu_provider_chug_get_firmware_version (item);

	if (item->got_version)
		g_debug ("ColorHug: DONE!");

	return TRUE;
}

/**
 * fu_provider_chug_open_cb:
 **/
static gboolean
fu_provider_chug_open_cb (gpointer user_data)
{
	FuProviderChugItem *item = (FuProviderChugItem *) user_data;

	g_debug ("attempt to open %s",
		 g_usb_device_get_platform_id (item->usb_device));
	fu_provider_chug_get_firmware_version (item);

	/* success! */
	if (item->got_version) {
		item->timeout_open_id = 0;
		return FALSE;
	}

	/* keep trying */
	return TRUE;
}

/**
 * fu_provider_chug_device_added_cb:
 **/
static void
fu_provider_chug_device_added_cb (GUsbContext *ctx,
				  GUsbDevice *device,
				  FuProviderChug *provider_chug)
{
	FuProviderChugPrivate *priv = GET_PRIVATE (provider_chug);
	FuProviderChugItem *item;
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
	device_key = fu_provider_chug_get_device_key (device);
	item = g_hash_table_lookup (priv->devices, device_key);
	if (item == NULL) {
		item = g_new0 (FuProviderChugItem, 1);
		item->loop = g_main_loop_new (NULL, FALSE);
		item->provider_chug = g_object_ref (provider_chug);
		item->usb_device = g_object_ref (device);
		item->device = fu_device_new ();
		fu_device_set_id (item->device, device_key);
		fu_device_set_equivalent_id (item->device,
					     g_usb_device_get_platform_id (device));
		fu_device_set_guid (item->device, ch_device_get_guid (device));
		fu_device_add_flag (item->device, FU_DEVICE_FLAG_ALLOW_OFFLINE);
		fu_device_add_flag (item->device, FU_DEVICE_FLAG_ALLOW_ONLINE);

		/* try to get the serial number -- if opening failed then
		 * poll until the device is not busy */
		fu_provider_chug_get_firmware_version (item);
		if (!item->got_version && item->timeout_open_id == 0) {
			item->timeout_open_id = g_timeout_add_seconds (FU_PROVIDER_CHUG_POLL_REOPEN,
				fu_provider_chug_open_cb, item);
		}

		/* insert to hash */
		g_hash_table_insert (priv->devices, g_strdup (device_key), item);
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
		fu_device_set_display_name (item->device, "ColorHug");
		break;
	case CH_DEVICE_MODE_BOOTLOADER2:
	case CH_DEVICE_MODE_FIRMWARE2:
		fu_device_set_display_name (item->device, "ColorHug2");
		break;
	case CH_DEVICE_MODE_BOOTLOADER_PLUS:
	case CH_DEVICE_MODE_FIRMWARE_PLUS:
		fu_device_set_display_name (item->device, "ColorHug+");
		break;
	case CH_DEVICE_MODE_BOOTLOADER_ALS:
	case CH_DEVICE_MODE_FIRMWARE_ALS:
		fu_device_set_display_name (item->device, "ColorHugALS");
		break;
	default:
		fu_device_set_display_name (item->device, "ColorHug??");
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
	fu_provider_device_add (FU_PROVIDER (provider_chug), item->device);

	/* are we waiting for the device to show up */
	if (g_main_loop_is_running (item->loop))
		g_main_loop_quit (item->loop);
}

/**
 * fu_provider_chug_device_removed_cb:
 **/
static void
fu_provider_chug_device_removed_cb (GUsbContext *ctx,
				    GUsbDevice *device,
				    FuProviderChug *provider_chug)
{
	FuProviderChugPrivate *priv = GET_PRIVATE (provider_chug);
	FuProviderChugItem *item;
	g_autofree gchar *device_key = NULL;

	/* already in database */
	device_key = fu_provider_chug_get_device_key (device);
	item = g_hash_table_lookup (priv->devices, device_key);
	if (item == NULL)
		return;

	/* no more polling for open */
	if (item->timeout_open_id != 0) {
		g_source_remove (item->timeout_open_id);
		item->timeout_open_id = 0;
	}
	fu_provider_device_remove (FU_PROVIDER (provider_chug), item->device);
}

/**
 * fu_provider_chug_coldplug:
 **/
static gboolean
fu_provider_chug_coldplug (FuProvider *provider, GError **error)
{
	FuProviderChug *provider_chug = FU_PROVIDER_CHUG (provider);
	FuProviderChugPrivate *priv = GET_PRIVATE (provider_chug);
	g_usb_context_enumerate (priv->usb_ctx);
	return TRUE;
}

/**
 * fu_provider_chug_class_init:
 **/
static void
fu_provider_chug_class_init (FuProviderChugClass *klass)
{
	FuProviderClass *provider_class = FU_PROVIDER_CLASS (klass);
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	provider_class->get_name = fu_provider_chug_get_name;
	provider_class->coldplug = fu_provider_chug_coldplug;
	provider_class->update_online = fu_provider_chug_update;
	provider_class->verify = fu_provider_chug_verify;
	object_class->finalize = fu_provider_chug_finalize;
}

/**
 * fu_provider_chug_init:
 **/
static void
fu_provider_chug_init (FuProviderChug *provider_chug)
{
	FuProviderChugPrivate *priv = GET_PRIVATE (provider_chug);
	priv->devices = g_hash_table_new_full (g_str_hash, g_str_equal,
					       g_free, (GDestroyNotify) fu_provider_chug_device_free);
	priv->usb_ctx = g_usb_context_new (NULL);
	priv->device_queue = ch_device_queue_new ();
	g_signal_connect (priv->usb_ctx, "device-added",
			  G_CALLBACK (fu_provider_chug_device_added_cb),
			  provider_chug);
	g_signal_connect (priv->usb_ctx, "device-removed",
			  G_CALLBACK (fu_provider_chug_device_removed_cb),
			  provider_chug);
}

/**
 * fu_provider_chug_finalize:
 **/
static void
fu_provider_chug_finalize (GObject *object)
{
	FuProviderChug *provider_chug = FU_PROVIDER_CHUG (object);
	FuProviderChugPrivate *priv = GET_PRIVATE (provider_chug);

	g_hash_table_unref (priv->devices);
	g_object_unref (priv->usb_ctx);
	g_object_unref (priv->device_queue);

	G_OBJECT_CLASS (fu_provider_chug_parent_class)->finalize (object);
}

/**
 * fu_provider_chug_new:
 **/
FuProvider *
fu_provider_chug_new (void)
{
	FuProviderChug *provider;
	provider = g_object_new (FU_TYPE_PROVIDER_CHUG, NULL);
	return FU_PROVIDER (provider);
}
