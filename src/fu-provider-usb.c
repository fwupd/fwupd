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

#include <appstream-glib.h>
#include <fwupd.h>
#include <glib-object.h>
#include <gusb.h>
#include <libdfu/dfu.h>

#include "fu-device.h"
#include "fu-provider-usb.h"

static void	fu_provider_usb_finalize	(GObject	*object);

/**
 * FuProviderUsbPrivate:
 **/
typedef struct {
	GHashTable		*devices;
	GUsbContext		*usb_ctx;
} FuProviderUsbPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (FuProviderUsb, fu_provider_usb, FU_TYPE_PROVIDER)
#define GET_PRIVATE(o) (fu_provider_usb_get_instance_private (o))

/**
 * fu_provider_usb_get_name:
 **/
static const gchar *
fu_provider_usb_get_name (FuProvider *provider)
{
	return "USB";
}

/**
 * fu_provider_usb_get_id:
 **/
static gchar *
fu_provider_usb_get_id (GUsbDevice *device)
{
	/* this identifies the *port* the device is plugged into */
	return g_strdup_printf ("usb-%s", g_usb_device_get_platform_id (device));
}

/**
 * fu_provider_usb_device_add:
 *
 * Important, the device must already be open!
 **/
static void
fu_provider_usb_device_add (FuProviderUsb *provider_usb, const gchar *id, GUsbDevice *device)
{
	FuProviderUsbPrivate *priv = GET_PRIVATE (provider_usb);
	FuDevice *dev;
	guint8 idx = 0x00;
	g_autofree gchar *guid = NULL;
	g_autofree gchar *product = NULL;
	g_autofree gchar *version = NULL;
	g_autoptr(AsProfile) profile = as_profile_new ();
	g_autoptr(AsProfileTask) ptask = NULL;
	g_autoptr(DfuDevice) dfu_device = NULL;
	g_autoptr(DfuTarget) dfu_target = NULL;

	/* get product */
	idx = g_usb_device_get_product_index (device);
	if (idx != 0x00) {
		g_autoptr(AsProfileTask) ptask2 = NULL;
		ptask2 = as_profile_start_literal (profile, "FuProviderUsb:get-string-desc");
		product = g_usb_device_get_string_descriptor (device, idx, NULL);
	}
	if (product == NULL) {
		g_debug ("ignoring %s as no product string descriptor", id);
		return;
	}

	/* get version number, falling back to the USB device release */
	ptask = as_profile_start_literal (profile, "FuProviderUsb:get-custom-index");
	idx = g_usb_device_get_custom_index (device,
					     G_USB_DEVICE_CLASS_VENDOR_SPECIFIC,
					     'F', 'W', NULL);
	if (idx != 0x00)
		version = g_usb_device_get_string_descriptor (device, idx, NULL);
	if (version == NULL) {
		guint16 release;
		release = g_usb_device_get_release (device);
		version = as_utils_version_from_uint16 (release,
							AS_VERSION_PARSE_FLAG_NONE);
	}

	/* get GUID, falling back to the USB VID:PID hash */
	idx = g_usb_device_get_custom_index (device,
					     G_USB_DEVICE_CLASS_VENDOR_SPECIFIC,
					     'G', 'U', NULL);
	if (idx != 0x00)
		guid = g_usb_device_get_string_descriptor (device, idx, NULL);
	if (guid == NULL) {
		g_autofree gchar *vid_pid = NULL;
		vid_pid = g_strdup_printf ("%04x:%04x",
					  g_usb_device_get_vid (device),
					  g_usb_device_get_pid (device));
		guid = as_utils_guid_from_string (vid_pid);
	}

	/* insert to hash */
	dev = fu_device_new ();
	fu_device_set_id (dev, id);
	fu_device_set_guid (dev, guid);
	fu_device_set_display_name (dev, product);
	fu_device_set_metadata (dev, FU_DEVICE_KEY_VERSION, version);

	/* is there a DFU interface */
	dfu_device = dfu_device_new (device);
	if (dfu_device != NULL) {
		dfu_target = dfu_device_get_target_by_alt_setting (dfu_device, 0, NULL);
		if (dfu_target != NULL) {
			if (dfu_target_can_download (dfu_target)) {
				fu_device_add_flag (dev, FU_DEVICE_FLAG_ALLOW_ONLINE);
				fu_device_add_flag (dev, FU_DEVICE_FLAG_ALLOW_OFFLINE);
			} else {
				g_debug ("DFU device does not support downloading?!");
				/* FIXME: is the CH+ correct here? */
				fu_device_add_flag (dev, FU_DEVICE_FLAG_ALLOW_ONLINE);
				fu_device_add_flag (dev, FU_DEVICE_FLAG_ALLOW_OFFLINE);
			}
		} else {
			g_debug ("not a DFU device");
		}
	}

	/* good to go */
	g_hash_table_insert (priv->devices, g_strdup (id), dev);
	fu_provider_device_add (FU_PROVIDER (provider_usb), dev);
}

/**
 * fu_provider_usb_device_added_cb:
 **/
static void
fu_provider_usb_device_added_cb (GUsbContext *ctx,
				 GUsbDevice *device,
				 FuProviderUsb *provider_usb)
{
	FuProviderUsbPrivate *priv = GET_PRIVATE (provider_usb);
	FuDevice *dev;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *id = NULL;
	g_autoptr(AsProfile) profile = as_profile_new ();
	g_autoptr(AsProfileTask) ptask = NULL;

	/* ignore hubs */
	if (g_usb_device_get_device_class (device) == G_USB_DEVICE_CLASS_HUB)
		return;
	ptask = as_profile_start (profile, "FuProviderUsb:added{%04x:%04x}",
				  g_usb_device_get_vid (device),
				  g_usb_device_get_pid (device));

	/* handled by another provider */
	id = fu_provider_usb_get_id (device);
	if (g_usb_device_get_vid (device) == 0x273f) {
		switch (g_usb_device_get_pid (device)) {
		case 0x1000:
		case 0x1001:
		case 0x1004:
		case 0x1005:
		case 0x1006:
		case 0x1007:
		case 0x1008:
			g_debug ("handling %s in another provider", id);
			return;
		default:
			break;
		}
	}

	/* is already in database */
	dev = g_hash_table_lookup (priv->devices, id);
	if (dev != NULL) {
		g_debug ("ignoring duplicate %s", id);
		return;
	}

	/* try to get the version without claiming interface */
	if (!g_usb_device_open (device, &error)) {
		g_debug ("Failed to open: %s", error->message);
		return;
	}

	/* try to add the device */
	fu_provider_usb_device_add (provider_usb, id, device);

	/* we're done here */
	if (!g_usb_device_close (device, &error))
		g_debug ("Failed to close: %s", error->message);
}

/**
 * fu_provider_usb_progress_cb:
 **/
static void
fu_provider_usb_progress_cb (DfuState state, goffset current,
			     goffset total, gpointer user_data)
{
	FuProvider *provider = FU_PROVIDER (user_data);
	switch (state) {
	case DFU_STATE_DFU_UPLOAD_IDLE:
		fu_provider_set_status (provider, FWUPD_STATUS_DEVICE_VERIFY);
		break;
	case DFU_STATE_DFU_DNLOAD_IDLE:
		fu_provider_set_status (provider, FWUPD_STATUS_DEVICE_WRITE);
		break;
	default:
		break;
	}
}

/**
 * fu_provider_usb_update:
 *
 * This updates using DFU.
 **/
static gboolean
fu_provider_usb_update (FuProvider *provider,
			FuDevice *device,
			GBytes *blob_fw,
			FuProviderFlags flags,
			GError **error)
{
	FuProviderUsb *provider_usb = FU_PROVIDER_USB (provider);
	FuProviderUsbPrivate *priv = GET_PRIVATE (provider_usb);
	GUsbDevice *dev;
	const gchar *platform_id;
	g_autoptr(DfuDevice) dfu_device = NULL;
	g_autoptr(DfuTarget) dfu_target = NULL;
	g_autoptr(GError) error_local = NULL;

	/* get device */
	platform_id = fu_device_get_id (device) + 4;
	dev = g_usb_context_find_by_platform_id (priv->usb_ctx, platform_id, &error_local);
	if (dev == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "cannot find device %s: %s",
			     platform_id, error_local->message);
		return FALSE;
	}

	/* open it */
	dfu_device = dfu_device_new (dev);
	if (dfu_device == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "%s is not a DFU device",
			     platform_id);
		return FALSE;
	}
	dfu_target = dfu_device_get_target_by_alt_setting (dfu_device, 0, error);
	if (dfu_target == NULL)
		return FALSE;
	if (!dfu_target_open (dfu_target, DFU_TARGET_OPEN_FLAG_NONE, NULL, error))
		return FALSE;
	g_debug ("device is now %s:%s",
		 dfu_mode_to_string (dfu_target_get_mode (dfu_target)),
		 dfu_state_to_string (dfu_target_get_state (dfu_target)));

	/* detach the device and wait for reconnection */
	if (dfu_target_get_mode (dfu_target) == DFU_MODE_RUNTIME) {
		if (!dfu_target_detach (dfu_target, NULL, error))
			return FALSE;
		if (!dfu_target_wait_for_reset (dfu_target, 5000, NULL, error))
			return FALSE;
	}

	/* hit hardware */
	if (!dfu_target_download (dfu_target, blob_fw,
				  DFU_TARGET_TRANSFER_FLAG_VERIFY |
				  DFU_TARGET_TRANSFER_FLAG_BOOT_RUNTIME,
				  NULL,
				  fu_provider_usb_progress_cb, provider,
				  error))
		return FALSE;

	/* teardown */
	if (!dfu_target_close (dfu_target, error))
		return FALSE;

	return TRUE;
}

/**
 * fu_provider_usb_device_removed_cb:
 **/
static void
fu_provider_usb_device_removed_cb (GUsbContext *ctx,
				   GUsbDevice *device,
				   FuProviderUsb *provider_usb)
{
	FuProviderUsbPrivate *priv = GET_PRIVATE (provider_usb);
	FuDevice *dev;
	g_autofree gchar *id = NULL;

	/* already in database */
	id = fu_provider_usb_get_id (device);
	dev = g_hash_table_lookup (priv->devices, id);
	if (dev == NULL)
		return;
	fu_provider_device_remove (FU_PROVIDER (provider_usb), dev);
}

/**
 * fu_provider_usb_coldplug:
 **/
static gboolean
fu_provider_usb_coldplug (FuProvider *provider, GError **error)
{
	FuProviderUsb *provider_usb = FU_PROVIDER_USB (provider);
	FuProviderUsbPrivate *priv = GET_PRIVATE (provider_usb);

	g_usb_context_enumerate (priv->usb_ctx);
	return TRUE;
}

/**
 * fu_provider_usb_verify:
 **/
static gboolean
fu_provider_usb_verify (FuProvider *provider,
			FuDevice *device,
			FuProviderVerifyFlags flags,
			GError **error)
{
	FuProviderUsb *provider_usb = FU_PROVIDER_USB (provider);
	FuProviderUsbPrivate *priv = GET_PRIVATE (provider_usb);
	GUsbDevice *dev;
	const gchar *platform_id;
	g_autofree gchar *hash = NULL;
	g_autoptr(DfuDevice) dfu_device = NULL;
	g_autoptr(DfuTarget) dfu_target = NULL;
	g_autoptr(GBytes) blob_fw = NULL;
	g_autoptr(GError) error_local = NULL;

	/* get device */
	platform_id = fu_device_get_id (device) + 4;
	dev = g_usb_context_find_by_platform_id (priv->usb_ctx, platform_id, &error_local);
	if (dev == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "cannot find device %s: %s",
			     platform_id, error_local->message);
		return FALSE;
	}

	/* open it */
	dfu_device = dfu_device_new (dev);
	if (dfu_device == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "%s is not a DFU device",
			     platform_id);
		return FALSE;
	}
	dfu_target = dfu_device_get_target_by_alt_setting (dfu_device, 0, error);
	if (dfu_target == NULL)
		return FALSE;
	if (!dfu_target_open (dfu_target, DFU_TARGET_OPEN_FLAG_NONE, NULL, error))
		return FALSE;
	g_debug ("device is now %s:%s",
		 dfu_mode_to_string (dfu_target_get_mode (dfu_target)),
		 dfu_state_to_string (dfu_target_get_state (dfu_target)));

	/* detach the device and wait for reconnection */
	if (dfu_target_get_mode (dfu_target) == DFU_MODE_RUNTIME) {
		if (!dfu_target_detach (dfu_target, NULL, error))
			return FALSE;
		if (!dfu_target_wait_for_reset (dfu_target, 5000, NULL, error))
			return FALSE;
	}

	/* get data from hardware */
	blob_fw = dfu_target_upload (dfu_target,
				     0,
				     DFU_TARGET_TRANSFER_FLAG_BOOT_RUNTIME,
				     NULL,
				     fu_provider_usb_progress_cb, provider,
				     error);
	if (blob_fw == NULL)
		return FALSE;

	/* teardown */
	if (!dfu_target_close (dfu_target, error))
		return FALSE;

	/* get the SHA1 hash */
	hash = g_compute_checksum_for_bytes (G_CHECKSUM_SHA1, blob_fw);
	fu_device_set_metadata (device, FU_DEVICE_KEY_FIRMWARE_HASH, hash);
	return TRUE;
}

/**
 * fu_provider_usb_class_init:
 **/
static void
fu_provider_usb_class_init (FuProviderUsbClass *klass)
{
	FuProviderClass *provider_class = FU_PROVIDER_CLASS (klass);
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	provider_class->get_name = fu_provider_usb_get_name;
	provider_class->coldplug = fu_provider_usb_coldplug;
	provider_class->update_online = fu_provider_usb_update;
	provider_class->verify = fu_provider_usb_verify;
	object_class->finalize = fu_provider_usb_finalize;
}

/**
 * fu_provider_usb_init:
 **/
static void
fu_provider_usb_init (FuProviderUsb *provider_usb)
{
	FuProviderUsbPrivate *priv = GET_PRIVATE (provider_usb);
	priv->devices = g_hash_table_new_full (g_str_hash, g_str_equal,
					       g_free, (GDestroyNotify) g_object_unref);
	priv->usb_ctx = g_usb_context_new (NULL);
	g_signal_connect (priv->usb_ctx, "device-added",
			  G_CALLBACK (fu_provider_usb_device_added_cb),
			  provider_usb);
	g_signal_connect (priv->usb_ctx, "device-removed",
			  G_CALLBACK (fu_provider_usb_device_removed_cb),
			  provider_usb);
}

/**
 * fu_provider_usb_finalize:
 **/
static void
fu_provider_usb_finalize (GObject *object)
{
	FuProviderUsb *provider_usb = FU_PROVIDER_USB (object);
	FuProviderUsbPrivate *priv = GET_PRIVATE (provider_usb);

	g_hash_table_unref (priv->devices);
	g_object_unref (priv->usb_ctx);

	G_OBJECT_CLASS (fu_provider_usb_parent_class)->finalize (object);
}

/**
 * fu_provider_usb_new:
 **/
FuProvider *
fu_provider_usb_new (void)
{
	FuProviderUsb *provider;
	provider = g_object_new (FU_TYPE_PROVIDER_USB, NULL);
	return FU_PROVIDER (provider);
}
