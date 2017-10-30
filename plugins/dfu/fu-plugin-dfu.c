/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
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

#include "fu-plugin.h"
#include "fu-plugin-vfuncs.h"

#include "dfu-context.h"

struct FuPluginData {
	DfuContext		*context;
};

static gchar *
_bcd_version_from_uint16 (guint16 val)
{
#if AS_CHECK_VERSION(0,7,3)
	return as_utils_version_from_uint16 (val, AS_VERSION_PARSE_FLAG_USE_BCD);
#else
	guint maj = ((val >> 12) & 0x0f) * 10 + ((val >> 8) & 0x0f);
	guint min = ((val >> 4) & 0x0f) * 10 + (val & 0x0f);
	return g_strdup_printf ("%u.%u", maj, min);
#endif
}

static gboolean
fu_plugin_dfu_device_update (FuPlugin *plugin,
			     FuDevice *dev,
			     DfuDevice *device,
			     GError **error)
{
	const gchar *platform_id;
	guint16 release;
	g_autofree gchar *version = NULL;
	g_autofree gchar *devid1 = NULL;
	g_autofree gchar *devid2 = NULL;
	g_autofree gchar *vendor_id = NULL;

	/* check mode */
	platform_id = dfu_device_get_platform_id (device);
	if (dfu_device_get_runtime_vid (device) == 0xffff) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "device not in runtime: %s",
			     platform_id);
		return FALSE;
	}

	/* check capabilities */
	if (dfu_device_can_download (device))
		fu_device_add_flag (dev, FWUPD_DEVICE_FLAG_UPDATABLE);

	/* needs a manual action */
	if (dfu_device_has_quirk (device, DFU_DEVICE_QUIRK_ACTION_REQUIRED)) {
		fu_device_add_flag (dev, FWUPD_DEVICE_FLAG_NEEDS_BOOTLOADER);
	} else {
		fu_device_remove_flag (dev, FWUPD_DEVICE_FLAG_NEEDS_BOOTLOADER);
	}

	/* get version number, falling back to the DFU device release */
	release = dfu_device_get_runtime_release (device);
	if (release != 0xffff) {
		version = _bcd_version_from_uint16 (release);
		fu_device_set_version (dev, version);
	}

	/* set vendor ID */
	vendor_id = g_strdup_printf ("USB:0x%04X", dfu_device_get_runtime_vid (device));
	fu_device_set_vendor_id (dev, vendor_id);

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
	return TRUE;
}

static void
fu_plugin_dfu_device_changed_cb (DfuContext *ctx,
				 DfuDevice *device,
				 FuPlugin *plugin)
{
	FuDevice *dev;
	const gchar *platform_id;
	g_autoptr(GError) error = NULL;

	/* convert DfuDevice to FuDevice */
	platform_id = dfu_device_get_platform_id (device);
	dev = fu_plugin_cache_lookup (plugin, platform_id);
	if (dev == NULL) {
		g_warning ("cannot find device %s", platform_id);
		return;
	}
	if (!fu_plugin_dfu_device_update (plugin, dev, device, &error)) {
		g_warning ("ignoring device: %s", error->message);
		return;
	}
}

static gboolean
dfu_device_open_no_refresh (DfuDevice *device, GError **error)
{
	return dfu_device_open_full (device, DFU_DEVICE_OPEN_FLAG_NO_AUTO_REFRESH,
				     NULL, error);
}

static void
fu_plugin_dfu_device_added_cb (DfuContext *ctx,
			       DfuDevice *device,
			       FuPlugin *plugin)
{
	const gchar *platform_id;
	const gchar *display_name;
	g_autoptr(AsProfile) profile = as_profile_new ();
	g_autoptr(AsProfileTask) ptask = NULL;
	g_autoptr(FuDevice) dev = NULL;
	g_autoptr(FuDeviceLocker) locker  = NULL;
	g_autoptr(GError) error = NULL;

	platform_id = dfu_device_get_platform_id (device);
	ptask = as_profile_start (profile, "FuPluginDfu:added{%s} [%04x:%04x]",
				  platform_id,
				  dfu_device_get_runtime_vid (device),
				  dfu_device_get_runtime_pid (device));
	g_assert (ptask != NULL);

	/* ignore defective runtimes */
	if (dfu_device_get_mode (device) == DFU_MODE_RUNTIME &&
	    dfu_device_has_quirk (device, DFU_DEVICE_QUIRK_IGNORE_RUNTIME)) {
		g_debug ("ignoring %s runtime", platform_id);
		return;
	}

	/* create new device */
	dev = fu_device_new ();
	fu_device_set_id (dev, platform_id);
	if (!fu_plugin_dfu_device_update (plugin, dev, device, &error)) {
		g_debug ("ignoring device: %s", error->message);
		return;
	}

	/* open device to get display name */
	locker = fu_device_locker_new_full (device,
					    (FuDeviceLockerFunc) dfu_device_open_no_refresh,
					    (FuDeviceLockerFunc) dfu_device_close,
					    &error);
	if (locker == NULL) {
		g_warning ("Failed to open DFU device: %s", error->message);
		return;
	}
	display_name = dfu_device_get_display_name (device);
	if (display_name != NULL)
		fu_device_set_name (dev, display_name);

	/* this is a guess and can be overridden in the metainfo file */
	fu_device_add_icon (dev, "drive-harddisk-usb");

	/* attempt to add */
	fu_plugin_device_add (plugin, dev);
	fu_plugin_cache_add (plugin, platform_id, dev);
}

static void
fu_plugin_dfu_device_removed_cb (DfuContext *ctx,
				 DfuDevice *device,
				 FuPlugin *plugin)
{
	FuDevice *dev;
	const gchar *platform_id;

	/* convert DfuDevice to FuDevice */
	platform_id = dfu_device_get_platform_id (device);
	dev = fu_plugin_cache_lookup (plugin, platform_id);
	if (dev == NULL) {
		g_warning ("cannot find device %s", platform_id);
		return;
	}

	fu_plugin_device_remove (plugin, dev);
}

static void
fu_plugin_dfu_state_changed_cb (DfuDevice *device,
				  DfuState state,
				  FuPlugin *plugin)
{
	switch (state) {
	case DFU_STATE_DFU_UPLOAD_IDLE:
		fu_plugin_set_status (plugin, FWUPD_STATUS_DEVICE_VERIFY);
		break;
	case DFU_STATE_DFU_DNLOAD_IDLE:
		fu_plugin_set_status (plugin, FWUPD_STATUS_DEVICE_WRITE);
		break;
	default:
		break;
	}
}

static void
fu_plugin_dfu_percentage_changed_cb (DfuDevice *device,
				       guint percentage,
				       FuPlugin *plugin)
{
	fu_plugin_set_percentage (plugin, percentage);
}

gboolean
fu_plugin_update (FuPlugin *plugin,
		  FuDevice *dev,
		  GBytes *blob_fw,
		  FwupdInstallFlags flags,
		  GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	DfuDevice *device;
	const gchar *platform_id;
	g_autoptr(DfuFirmware) dfu_firmware = NULL;
	g_autoptr(FuDeviceLocker) locker  = NULL;
	g_autoptr(GError) error_local = NULL;

	/* get device */
	platform_id = fu_device_get_id (dev);
	device = dfu_context_get_device_by_platform_id (data->context,
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
	locker = fu_device_locker_new_full (device,
					    (FuDeviceLockerFunc) dfu_device_open,
					    (FuDeviceLockerFunc) dfu_device_close,
					    &error_local);
	if (locker == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "failed to open DFU device %s: %s",
			     platform_id, error_local->message);
		return FALSE;
	}
	g_signal_connect (device, "state-changed",
			  G_CALLBACK (fu_plugin_dfu_state_changed_cb), plugin);
	g_signal_connect (device, "percentage-changed",
			  G_CALLBACK (fu_plugin_dfu_percentage_changed_cb), plugin);

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
	fu_plugin_set_status (plugin, FWUPD_STATUS_IDLE);
	return TRUE;
}

gboolean
fu_plugin_verify (FuPlugin *plugin,
		  FuDevice *dev,
		  FuPluginVerifyFlags flags,
		  GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	GBytes *blob_fw;
	DfuDevice *device;
	const gchar *platform_id;
	g_autoptr(DfuFirmware) dfu_firmware = NULL;
	g_autoptr(FuDeviceLocker) locker  = NULL;
	g_autoptr(GError) error_local = NULL;
	GChecksumType checksum_types[] = {
		G_CHECKSUM_SHA1,
		G_CHECKSUM_SHA256,
		0 };

	/* get device */
	platform_id = fu_device_get_id (dev);
	device = dfu_context_get_device_by_platform_id (data->context,
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
	locker = fu_device_locker_new_full (device,
					    (FuDeviceLockerFunc) dfu_device_open,
					    (FuDeviceLockerFunc) dfu_device_close,
					    &error_local);
	if (locker == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "failed to open DFU device %s: %s",
			     platform_id, error_local->message);
		return FALSE;
	}
	g_signal_connect (device, "state-changed",
			  G_CALLBACK (fu_plugin_dfu_state_changed_cb), plugin);
	g_signal_connect (device, "percentage-changed",
			  G_CALLBACK (fu_plugin_dfu_percentage_changed_cb), plugin);

	/* get data from hardware */
	g_debug ("uploading from device->host");
	dfu_firmware = dfu_device_upload (device,
					  DFU_TARGET_TRANSFER_FLAG_DETACH |
					  DFU_TARGET_TRANSFER_FLAG_WAIT_RUNTIME,
					  NULL,
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
	fu_plugin_set_status (plugin, FWUPD_STATUS_IDLE);
	return TRUE;
}

void
fu_plugin_init (FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_alloc_data (plugin, sizeof (FuPluginData));
	GUsbContext *usb_ctx = fu_plugin_get_usb_context (plugin);
	FuQuirks *quirks = fu_plugin_get_quirks (plugin);
	data->context = dfu_context_new_full (usb_ctx, quirks);
}

void
fu_plugin_destroy (FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	g_object_unref (data->context);
}

gboolean
fu_plugin_startup (FuPlugin *plugin, GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	g_signal_connect (data->context, "device-added",
			  G_CALLBACK (fu_plugin_dfu_device_added_cb),
			  plugin);
	g_signal_connect (data->context, "device-removed",
			  G_CALLBACK (fu_plugin_dfu_device_removed_cb),
			  plugin);
	g_signal_connect (data->context, "device-changed",
			  G_CALLBACK (fu_plugin_dfu_device_changed_cb),
			  plugin);
	return TRUE;
}
