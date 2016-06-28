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
#include <libdfu/dfu.h>

#include "fu-device.h"
#include "fu-provider-dfu.h"

static void	fu_provider_dfu_finalize	(GObject	*object);

/**
 * FuProviderDfuPrivate:
 **/
typedef struct {
	DfuContext		*context;
	GHashTable		*devices;	/* platform_id:DfuDevice */
} FuProviderDfuPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (FuProviderDfu, fu_provider_dfu, FU_TYPE_PROVIDER)
#define GET_PRIVATE(o) (fu_provider_dfu_get_instance_private (o))

/**
 * fu_provider_dfu_get_name:
 **/
static const gchar *
fu_provider_dfu_get_name (FuProvider *provider)
{
	return "DFU";
}

/**
 * fu_provider_dfu_device_update:
 **/
static void
fu_provider_dfu_device_update (FuProviderDfu *provider_dfu,
			       FuDevice *dev,
			       DfuDevice *device)
{
	const gchar *platform_id;
	guint16 release;
	g_autofree gchar *guid = NULL;
	g_autofree gchar *version = NULL;
	g_autofree gchar *devid1 = NULL;
	g_autofree gchar *devid2 = NULL;

	/* check mode */
	platform_id = dfu_device_get_platform_id (device);
	if (dfu_device_get_runtime_vid (device) == 0xffff) {
		g_debug ("Ignoring DFU device not in runtime: %s", platform_id);
		return;
	}

	/* check capabilities */
	if (dfu_device_can_download (device)) {
		fu_device_add_flag (dev, FU_DEVICE_FLAG_ALLOW_ONLINE);
		fu_device_add_flag (dev, FU_DEVICE_FLAG_ALLOW_OFFLINE);
	}

	/* get version number, falling back to the DFU device release */
	release = dfu_device_get_runtime_release (device);
	if (release != 0xffff) {
		version = as_utils_version_from_uint16 (release,
							AS_VERSION_PARSE_FLAG_NONE);
		fu_device_set_version (dev, version);
	}

	/* add USB\VID_0000&PID_0000 */
	devid1 = g_strdup_printf ("USB\\VID_%04X&PID_%04X",
				  dfu_device_get_runtime_vid (device),
				  dfu_device_get_runtime_pid (device));
	fu_device_add_guid (dev, devid1);

	/* add more specific USB\VID_0000&PID_0000&REV_0000 */
	devid2 = g_strdup_printf ("USB\\VID_%04X&PID_%04X&REV_%04X",
				  dfu_device_get_runtime_vid (device),
				  dfu_device_get_runtime_pid (device),
				  dfu_device_get_runtime_release (device));
	fu_device_add_guid (dev, devid2);
}

/**
 * fu_provider_dfu_device_changed_cb:
 **/
static void
fu_provider_dfu_device_changed_cb (DfuContext *ctx,
				   DfuDevice *device,
				   FuProviderDfu *provider_dfu)
{
	FuProviderDfuPrivate *priv = GET_PRIVATE (provider_dfu);
	FuDevice *dev;
	const gchar *platform_id;

	/* convert DfuDevice to FuDevice */
	platform_id = dfu_device_get_platform_id (device);
	dev = g_hash_table_lookup (priv->devices, platform_id);
	if (dev == NULL) {
		g_warning ("cannot find device %s", platform_id);
		return;
	}
	fu_provider_dfu_device_update (provider_dfu, dev, device);
}

/**
 * fu_provider_dfu_device_added_cb:
 **/
static void
fu_provider_dfu_device_added_cb (DfuContext *ctx,
				 DfuDevice *device,
				 FuProviderDfu *provider_dfu)
{
	FuProviderDfuPrivate *priv = GET_PRIVATE (provider_dfu);
	const gchar *platform_id;
	const gchar *display_name;
	g_autofree gchar *id = NULL;
	g_autoptr(AsProfile) profile = as_profile_new ();
	g_autoptr(AsProfileTask) ptask = NULL;
	g_autoptr(FuDevice) dev = NULL;
	g_autoptr(GError) error = NULL;

	platform_id = dfu_device_get_platform_id (device);
	ptask = as_profile_start (profile, "FuProviderDfu:added{%s} [%04x:%04x]",
				  platform_id,
				  dfu_device_get_runtime_vid (device),
				  dfu_device_get_runtime_pid (device));

	/* ignore defective runtimes */
	if (dfu_device_get_mode (device) == DFU_MODE_RUNTIME &&
	    dfu_device_has_quirk (device, DFU_DEVICE_QUIRK_IGNORE_RUNTIME)) {
		g_debug ("ignoring %s runtime", platform_id);
		return;
	}

	/* create new device */
	dev = fu_device_new ();
	fu_device_set_id (dev, platform_id);
	fu_provider_dfu_device_update (provider_dfu, dev, device);

	/* open device to get display name */
	if (!dfu_device_open (device, DFU_DEVICE_OPEN_FLAG_NO_AUTO_REFRESH,
			      NULL, &error)) {
		g_warning ("Failed to open DFU device: %s", error->message);
		return;
	}
	display_name = dfu_device_get_display_name (device);
	if (display_name != NULL)
		fu_device_set_name (dev, display_name);

	/* we're done here */
	if (!dfu_device_close (device, &error))
		g_debug ("Failed to close %s: %s", platform_id, error->message);

	/* attempt to add */
	fu_provider_device_add (FU_PROVIDER (provider_dfu), dev);
	g_hash_table_insert (priv->devices,
			     g_strdup (platform_id),
			     g_object_ref (dev));
}

/**
 * fu_provider_dfu_device_removed_cb:
 **/
static void
fu_provider_dfu_device_removed_cb (DfuContext *ctx,
				   DfuDevice *device,
				   FuProviderDfu *provider_dfu)
{
	FuProviderDfuPrivate *priv = GET_PRIVATE (provider_dfu);
	FuDevice *dev;
	const gchar *platform_id;

	/* convert DfuDevice to FuDevice */
	platform_id = dfu_device_get_platform_id (device);
	dev = g_hash_table_lookup (priv->devices, platform_id);
	if (dev == NULL) {
		g_warning ("cannot find device %s", platform_id);
		return;
	}

	fu_provider_device_remove (FU_PROVIDER (provider_dfu), dev);
}

/**
 * fu_provider_dfu_coldplug:
 **/
static gboolean
fu_provider_dfu_coldplug (FuProvider *provider, GError **error)
{
	FuProviderDfu *provider_dfu = FU_PROVIDER_DFU (provider);
	FuProviderDfuPrivate *priv = GET_PRIVATE (provider_dfu);
	dfu_context_enumerate (priv->context, NULL);
	return TRUE;
}

/**
 * fu_provider_dfu_state_changed_cb:
 **/
static void
fu_provider_dfu_state_changed_cb (DfuDevice *device,
				  DfuState state,
				  FuProvider *provider)
{
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
 * fu_provider_dfu_update:
 *
 * This updates using DFU.
 **/
static gboolean
fu_provider_dfu_update (FuProvider *provider,
			FuDevice *dev,
			GBytes *blob_fw,
			FwupdInstallFlags flags,
			GError **error)
{
	FuProviderDfu *provider_dfu = FU_PROVIDER_DFU (provider);
	FuProviderDfuPrivate *priv = GET_PRIVATE (provider_dfu);
	DfuDevice *device;
	const gchar *platform_id;
	g_autoptr(DfuDevice) dfu_device = NULL;
	g_autoptr(DfuFirmware) dfu_firmware = NULL;
	g_autoptr(GError) error_local = NULL;

	/* get device */
	platform_id = fu_device_get_id (dev);
	device = dfu_context_get_device_by_platform_id (priv->context,
							platform_id,
							&error_local);
	if (device == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "cannot find device %s: %s",
			     platform_id, error_local->message);
		return FALSE;
	}

	/* open it */
	if (!dfu_device_open (device, DFU_DEVICE_OPEN_FLAG_NONE,
			      NULL, &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "failed to open DFU device %s: %s",
			     platform_id, error_local->message);
		return FALSE;
	}
	g_signal_connect (device, "state-changed",
			  G_CALLBACK (fu_provider_dfu_state_changed_cb), provider);

	/* hit hardware */
	dfu_firmware = dfu_firmware_new ();
	if (!dfu_firmware_parse_data (dfu_firmware, blob_fw,
				      DFU_FIRMWARE_PARSE_FLAG_NONE, error))
		return FALSE;
	if (!dfu_device_download (device, dfu_firmware,
				  DFU_TARGET_TRANSFER_FLAG_DETACH |
				  DFU_TARGET_TRANSFER_FLAG_VERIFY |
				  DFU_TARGET_TRANSFER_FLAG_WAIT_RUNTIME,
				  NULL,
				  error))
		return FALSE;

	/* we're done */
	if (!dfu_device_close (device, &error_local)) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     error_local->message);
		return FALSE;
	}
	fu_provider_set_status (provider, FWUPD_STATUS_IDLE);
	return TRUE;
}

/**
 * fu_provider_dfu_verify:
 **/
static gboolean
fu_provider_dfu_verify (FuProvider *provider,
			FuDevice *dev,
			FuProviderVerifyFlags flags,
			GError **error)
{
	FuProviderDfu *provider_dfu = FU_PROVIDER_DFU (provider);
	FuProviderDfuPrivate *priv = GET_PRIVATE (provider_dfu);
	GBytes *blob_fw;
	GChecksumType checksum_type;
	DfuDevice *device;
	const gchar *platform_id;
	g_autofree gchar *hash = NULL;
	g_autoptr(DfuDevice) dfu_device = NULL;
	g_autoptr(DfuFirmware) dfu_firmware = NULL;
	g_autoptr(GError) error_local = NULL;

	/* get device */
	platform_id = fu_device_get_id (dev);
	device = dfu_context_get_device_by_platform_id (priv->context,
							platform_id,
							&error_local);
	if (device == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "cannot find device %s: %s",
			     platform_id, error_local->message);
		return FALSE;
	}

	/* open it */
	if (!dfu_device_open (device, DFU_DEVICE_OPEN_FLAG_NONE,
			      NULL, &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "failed to open DFU device %s: %s",
			     platform_id, error_local->message);
		return FALSE;
	}
	g_signal_connect (device, "state-changed",
			  G_CALLBACK (fu_provider_dfu_state_changed_cb), provider);

	/* get data from hardware */
	g_debug ("uploading from device->host");
	dfu_firmware = dfu_device_upload (device,
					  DFU_TARGET_TRANSFER_FLAG_DETACH |
					  DFU_TARGET_TRANSFER_FLAG_WAIT_RUNTIME,
					  NULL,
					  error);
	if (dfu_firmware == NULL)
		return FALSE;

	/* we're done */
	if (!dfu_device_close (device, &error_local)) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     error_local->message);
		return FALSE;
	}

	/* get the checksum */
	blob_fw = dfu_firmware_write_data (dfu_firmware, error);
	if (blob_fw == NULL)
		return FALSE;
	checksum_type = fu_provider_get_checksum_type (flags);
	hash = g_compute_checksum_for_bytes (checksum_type, blob_fw);
	fu_device_set_checksum (dev, hash);
	fu_device_set_checksum_kind (dev, checksum_type);
	fu_provider_set_status (provider, FWUPD_STATUS_IDLE);
	return TRUE;
}

/**
 * fu_provider_dfu_class_init:
 **/
static void
fu_provider_dfu_class_init (FuProviderDfuClass *klass)
{
	FuProviderClass *provider_class = FU_PROVIDER_CLASS (klass);
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	provider_class->get_name = fu_provider_dfu_get_name;
	provider_class->coldplug = fu_provider_dfu_coldplug;
	provider_class->update_online = fu_provider_dfu_update;
	provider_class->verify = fu_provider_dfu_verify;
	object_class->finalize = fu_provider_dfu_finalize;
}

/**
 * fu_provider_dfu_init:
 **/
static void
fu_provider_dfu_init (FuProviderDfu *provider_dfu)
{
	FuProviderDfuPrivate *priv = GET_PRIVATE (provider_dfu);
	priv->devices = g_hash_table_new_full (g_str_hash, g_str_equal,
					       g_free, (GDestroyNotify) g_object_unref);
	priv->context = dfu_context_new ();
	g_signal_connect (priv->context, "device-added",
			  G_CALLBACK (fu_provider_dfu_device_added_cb),
			  provider_dfu);
	g_signal_connect (priv->context, "device-removed",
			  G_CALLBACK (fu_provider_dfu_device_removed_cb),
			  provider_dfu);
	g_signal_connect (priv->context, "device-changed",
			  G_CALLBACK (fu_provider_dfu_device_changed_cb),
			  provider_dfu);
}

/**
 * fu_provider_dfu_finalize:
 **/
static void
fu_provider_dfu_finalize (GObject *object)
{
	FuProviderDfu *provider_dfu = FU_PROVIDER_DFU (object);
	FuProviderDfuPrivate *priv = GET_PRIVATE (provider_dfu);

	g_hash_table_unref (priv->devices);
	g_object_unref (priv->context);

	G_OBJECT_CLASS (fu_provider_dfu_parent_class)->finalize (object);
}

/**
 * fu_provider_dfu_new:
 **/
FuProvider *
fu_provider_dfu_new (void)
{
	FuProviderDfu *provider;
	provider = g_object_new (FU_TYPE_PROVIDER_DFU, NULL);
	return FU_PROVIDER (provider);
}
