/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

/**
 * SECTION:dfu-device
 * @short_description: Object representing a DFU-capable device
 *
 * This object allows two things:
 *
 *  - Downloading from the host to the device, optionally with
 *    verification using a DFU or DfuSe firmware file.
 *
 *  - Uploading from the device to the host to a DFU or DfuSe firmware
 *    file. The file format is chosen automatically, with DfuSe being
 *    chosen if the device contains more than one target.
 *
 * See also: #DfuTarget, #DfuFirmware
 */

#include "config.h"

#include <string.h>

#include "dfu-common.h"
#include "dfu-device-private.h"
#include "dfu-error.h"
#include "dfu-target-private.h"

static void dfu_device_finalize			 (GObject *object);

/**
 * DfuDevicePrivate:
 *
 * Private #DfuDevice data
 **/
typedef struct {
	GUsbDevice		*dev;
	GPtrArray		*targets;
	gboolean		 device_open;
	guint16			 runtime_pid;
	guint16			 runtime_vid;
} DfuDevicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (DfuDevice, dfu_device, G_TYPE_OBJECT)
#define GET_PRIVATE(o) (dfu_device_get_instance_private (o))

/**
 * dfu_device_class_init:
 **/
static void
dfu_device_class_init (DfuDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = dfu_device_finalize;
}

/**
 * dfu_device_init:
 **/
static void
dfu_device_init (DfuDevice *device)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	priv->runtime_vid = 0xffff;
	priv->runtime_pid = 0xffff;
	priv->targets = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
}

/**
 * dfu_device_finalize:
 **/
static void
dfu_device_finalize (GObject *object)
{
	DfuDevice *device = DFU_DEVICE (object);
	DfuDevicePrivate *priv = GET_PRIVATE (device);

	/* don't rely on this */
	if (priv->device_open) {
		g_debug ("auto-closing DfuDevice, call dfu_device_close()");
		g_usb_device_close (priv->dev, NULL);
	}

	g_ptr_array_unref (priv->targets);

	G_OBJECT_CLASS (dfu_device_parent_class)->finalize (object);
}

/**
 * dfu_device_add_targets:
 **/
static gboolean
dfu_device_add_targets (DfuDevice *device)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	guint i;
	GUsbInterface *iface;
	g_autoptr(GPtrArray) ifaces = NULL;

	/* add all DFU-capable targets */
	ifaces = g_usb_device_get_interfaces (priv->dev, NULL);
	if (ifaces == NULL)
		return FALSE;
	g_ptr_array_set_size (priv->targets, 0);
	for (i = 0; i < ifaces->len; i++) {
		DfuTarget *target;
		iface = g_ptr_array_index (ifaces, i);
		if (g_usb_interface_get_class (iface) != G_USB_DEVICE_CLASS_APPLICATION_SPECIFIC)
			continue;
		if (g_usb_interface_get_subclass (iface) != 0x01)
			continue;
		target = _dfu_target_new (device, iface);
		if (target == NULL)
			continue;
		g_ptr_array_add (priv->targets, target);
	}
	return priv->targets->len > 0;
}

/**
 * dfu_device_new:
 * @dev: A #GUsbDevice
 *
 * Creates a new DFU device object.
 *
 * Return value: a new #DfuDevice, or %NULL if @dev was not DFU-capable
 *
 * Since: 0.5.4
 **/
DfuDevice *
dfu_device_new (GUsbDevice *dev)
{
	DfuDevicePrivate *priv;
	DfuDevice *device;
	device = g_object_new (DFU_TYPE_DEVICE, NULL);
	priv = GET_PRIVATE (device);
	priv->dev = g_object_ref (dev);
	if (!dfu_device_add_targets (device)) {
		g_object_unref (device);
		return NULL;
	}
	return device;
}

/**
 * dfu_device_get_targets:
 * @device: a #DfuDevice
 *
 * Gets all the targets for this device.
 *
 * Return value: (transfer none): (element-type DfuTarget): #DfuTarget, or %NULL
 *
 * Since: 0.5.4
 **/
GPtrArray *
dfu_device_get_targets (DfuDevice *device)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	g_return_val_if_fail (DFU_IS_DEVICE (device), NULL);
	return priv->targets;
}

/**
 * dfu_device_get_target_by_alt_setting:
 * @device: a #DfuDevice
 * @alt_setting: the setting used to find
 * @error: a #GError, or %NULL
 *
 * Gets a target with a specific alternative setting.
 *
 * Return value: (transfer full): a #DfuTarget, or %NULL
 *
 * Since: 0.5.4
 **/
DfuTarget *
dfu_device_get_target_by_alt_setting (DfuDevice *device,
				      guint8 alt_setting,
				      GError **error)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	DfuTarget *target;
	guint i;

	g_return_val_if_fail (DFU_IS_DEVICE (device), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* find by ID */
	for (i = 0; i < priv->targets->len; i++) {
		target = g_ptr_array_index (priv->targets, i);
		if (dfu_target_get_interface_alt_setting (target) == alt_setting)
			return g_object_ref (target);
	}

	/* failed */
	g_set_error (error,
		     DFU_ERROR,
		     DFU_ERROR_NOT_FOUND,
		     "No target with alt-setting %i",
		     alt_setting);
	return NULL;
}

/**
 * dfu_device_get_target_default:
 * @device: a #DfuDevice
 * @error: a #GError, or %NULL
 *
 * Gets the default target.
 *
 * Return value: (transfer full): a #DfuTarget, or %NULL
 *
 * Since: 0.5.4
 **/
DfuTarget *
dfu_device_get_target_default (DfuDevice *device, GError **error)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);

	g_return_val_if_fail (DFU_IS_DEVICE (device), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* find first target */
	if (priv->targets->len == 0) {
		g_set_error_literal (error,
				     DFU_ERROR,
				     DFU_ERROR_NOT_FOUND,
				     "No default target");
		return NULL;
	}
	return g_object_ref (g_ptr_array_index (priv->targets, 0));
}

/**
 * dfu_device_get_target_by_alt_name:
 * @device: a #DfuDevice
 * @alt_name: the name used to find
 * @error: a #GError, or %NULL
 *
 * Gets a target with a specific alternative name.
 *
 * Return value: (transfer full): a #DfuTarget, or %NULL
 *
 * Since: 0.5.4
 **/
DfuTarget *
dfu_device_get_target_by_alt_name (DfuDevice *device,
				   const gchar *alt_name,
				   GError **error)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	DfuTarget *target;
	guint i;

	g_return_val_if_fail (DFU_IS_DEVICE (device), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* find by ID */
	for (i = 0; i < priv->targets->len; i++) {
		target = g_ptr_array_index (priv->targets, i);
		if (g_strcmp0 (dfu_target_get_interface_alt_name (target), alt_name) == 0)
			return g_object_ref (target);
	}

	/* failed */
	g_set_error (error,
		     DFU_ERROR,
		     DFU_ERROR_NOT_FOUND,
		     "No target with alt-name %s",
		     alt_name);
	return NULL;
}

/**
 * dfu_device_get_runtime_vid:
 * @device: a #DfuDevice
 *
 * Gets the runtime vendor ID.
 *
 * Return value: vendor ID, or 0xffff for unknown
 *
 * Since: 0.5.4
 **/
guint16
dfu_device_get_runtime_vid (DfuDevice *device)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	g_return_val_if_fail (DFU_IS_DEVICE (device), 0xffff);
	return priv->runtime_vid;
}

/**
 * dfu_device_get_runtime_pid:
 * @device: a #DfuDevice
 *
 * Gets the runtime product ID.
 *
 * Return value: product ID, or 0xffff for unknown
 *
 * Since: 0.5.4
 **/
guint16
dfu_device_get_runtime_pid (DfuDevice *device)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	g_return_val_if_fail (DFU_IS_DEVICE (device), 0xffff);
	return priv->runtime_pid;
}

/**
 * _dfu_device_set_runtime_vid:
 * @device: a #DfuDevice
 * @runtime_vid: a vendor ID, or 0xffff for unknown
 *
 * Sets the runtime vendor ID.
 *
 * Since: 0.5.4
 **/
void
_dfu_device_set_runtime_vid (DfuDevice *device, guint16 runtime_vid)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	g_return_if_fail (DFU_IS_DEVICE (device));
	priv->runtime_vid = runtime_vid;
}

/**
 * _dfu_device_set_runtime_pid:
 * @device: a #DfuDevice
 * @runtime_pid: a product ID, or 0xffff for unknown
 *
 * Sets the runtime product ID.
 *
 * Since: 0.5.4
 **/
void
_dfu_device_set_runtime_pid (DfuDevice *device, guint16 runtime_pid)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	g_return_if_fail (DFU_IS_DEVICE (device));
	priv->runtime_pid = runtime_pid;
}

/**
 * _dfu_device_get_usb_dev: (skip)
 * @device: a #DfuDevice
 *
 * Gets the internal USB device for the #DfuDevice.
 *
 * NOTE: This may change at runtime if the device is replugged or
 * reset.
 *
 * Returns: (transfer none): the internal USB device
 **/
GUsbDevice *
_dfu_device_get_usb_dev (DfuDevice *device)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	g_return_val_if_fail (DFU_IS_DEVICE (device), NULL);
	return priv->dev;
}

/**
 * dfu_device_open:
 * @device: a #DfuDevice
 * @error: a #GError, or %NULL
 *
 * Opens a DFU-capable device.
 *
 * Return value: %TRUE for success
 *
 * Since: 0.5.4
 **/
gboolean
dfu_device_open (DfuDevice *device, GError **error)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail (DFU_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* just ignore */
	if (priv->device_open)
		return TRUE;

	/* open */
	if (!g_usb_device_open (priv->dev, &error_local)) {
		g_set_error (error,
			     DFU_ERROR,
			     DFU_ERROR_INVALID_DEVICE,
			     "cannot open device %s: %s",
			     g_usb_device_get_platform_id (priv->dev),
			     error_local->message);
		return FALSE;
	}
	priv->device_open = TRUE;

	return TRUE;
}

/**
 * dfu_device_close:
 * @device: a #DfuDevice
 * @error: a #GError, or %NULL
 *
 * Closes a DFU device.
 *
 * Return value: %TRUE for success
 *
 * Since: 0.5.4
 **/
gboolean
dfu_device_close (DfuDevice *device, GError **error)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);

	/* only close if open */
	if (priv->device_open) {
		if (!g_usb_device_close (priv->dev, error))
			return FALSE;
		priv->device_open = FALSE;
	}
	return TRUE;
}

/**
 * dfu_device_set_new_usb_dev:
 **/
static gboolean
dfu_device_set_new_usb_dev (DfuDevice *device, GUsbDevice *dev,
			    GCancellable *cancellable, GError **error)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	guint i;
	GUsbInterface *iface;
	g_autoptr(GPtrArray) ifaces = NULL;

	/* mark all existing interfaces as unclaimed */
	for (i = 0; i < priv->targets->len; i++) {
		DfuTarget *target = g_ptr_array_index (priv->targets, i);
		dfu_target_close (target, NULL);
	}

	/* close existing device */
	if (!dfu_device_close (device, error))
		return FALSE;

	/* set the new USB device */
	g_set_object (&priv->dev, dev);

	/* update each interface */
	ifaces = g_usb_device_get_interfaces (dev, error);
	if (ifaces == NULL)
		return FALSE;
	for (i = 0; i < ifaces->len; i++) {
		guint8 alt_setting;
		g_autoptr(DfuTarget) target = NULL;
		iface = g_ptr_array_index (ifaces, i);
		if (g_usb_interface_get_class (iface) != G_USB_DEVICE_CLASS_APPLICATION_SPECIFIC)
			continue;
		if (g_usb_interface_get_subclass (iface) != 0x01)
			continue;
		alt_setting = g_usb_interface_get_alternate (iface);
		target = dfu_device_get_target_by_alt_setting (device, alt_setting, NULL);
		if (target == NULL)
			continue;
		if (!_dfu_target_update (target, iface, cancellable, error))
			return FALSE;
	}
	return dfu_device_open (device, error);
}

/**
 * dfu_device_wait_for_replug:
 * @device: a #DfuDevice
 * @timeout: the maximum amount of time to wait
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Waits for a DFU device to disconnect and reconnect.
 *
 * Return value: %TRUE for success
 *
 * Since: 0.5.4
 **/
gboolean
dfu_device_wait_for_replug (DfuDevice *device, guint timeout,
			    GCancellable *cancellable, GError **error)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	const guint poll_interval_ms = 100;
	gboolean went_away = FALSE;
	guint16 pid;
	guint16 vid;
	guint i;
	g_autofree gchar *platform_id = NULL;
	g_autoptr(GUsbContext) usb_ctx = NULL;

	g_return_val_if_fail (DFU_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* keep copies */
	platform_id = g_strdup (g_usb_device_get_platform_id (priv->dev));
	vid = g_usb_device_get_vid (priv->dev);
	pid = g_usb_device_get_pid (priv->dev);

	/* keep trying */
	g_object_get (priv->dev, "context", &usb_ctx, NULL);
	for (i = 0; i < timeout / poll_interval_ms; i++) {
		g_autoptr(GUsbDevice) dev_tmp = NULL;
		g_usleep (poll_interval_ms * 1000);
		g_usb_context_enumerate (usb_ctx);
		dev_tmp = g_usb_context_find_by_platform_id (usb_ctx, platform_id, NULL);
		if (dev_tmp == NULL) {
			went_away = TRUE;
			continue;
		}

		/* VID:PID changed so find a DFU iface with the same alt */
		if (vid != g_usb_device_get_vid (dev_tmp) ||
		    pid != g_usb_device_get_pid (dev_tmp)) {
			return dfu_device_set_new_usb_dev (device, dev_tmp,
							   cancellable, error);
		}
	}

	/* target went off into the woods */
	if (went_away) {
		g_set_error_literal (error,
				     DFU_ERROR,
				     DFU_ERROR_INVALID_DEVICE,
				     "target went away but did not come back");
		return FALSE;
	}

	/* VID and PID did not change */
	g_set_error_literal (error,
			     DFU_ERROR,
			     DFU_ERROR_INVALID_DEVICE,
			     "target came back with same VID:PID values");
	return FALSE;
}

/**
 * dfu_device_reset:
 * @device: a #DfuDevice
 * @error: a #GError, or %NULL
 *
 * Resets the USB device.
 *
 * Return value: %TRUE for success
 *
 * Since: 0.5.4
 **/
gboolean
dfu_device_reset (DfuDevice *device, GError **error)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail (DFU_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (!g_usb_device_reset (priv->dev, &error_local)) {
		g_set_error (error,
			     DFU_ERROR,
			     DFU_ERROR_INVALID_DEVICE,
			     "cannot reset USB device: %s [%i]",
			     error_local->message,
			     error_local->code);
		return FALSE;
	}
	return TRUE;
}

/**
 * dfu_device_upload:
 * @device: a #DfuDevice
 * @expected_size: the expected size of the firmware, or 0 for unknown
 * @flags: flags to use, e.g. %DFU_TARGET_TRANSFER_FLAG_VERIFY
 * @cancellable: a #GCancellable, or %NULL
 * @progress_cb: a #GFileProgressCallback, or %NULL
 * @progress_cb_data: user data to pass to @progress_cb
 * @error: a #GError, or %NULL
 *
 * Uploads firmware from the target to the host.
 *
 * Return value: (transfer full): the uploaded firmware, or %NULL for error
 *
 * Since: 0.5.4
 **/
DfuFirmware *
dfu_device_upload (DfuDevice *device,
		   gsize expected_size,
		   DfuTargetTransferFlags flags,
		   GCancellable *cancellable,
		   DfuProgressCallback progress_cb,
		   gpointer progress_cb_data,
		   GError **error)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	gboolean auto_opened = FALSE;
	guint i;
	g_autoptr(DfuFirmware) firmware = NULL;
	g_autoptr(DfuTarget) target_default = NULL;
	g_autoptr(GPtrArray) targets = NULL;

	/* auto-open */
	if (!priv->device_open) {
		if (!dfu_device_open (device, error))
			return FALSE;
		auto_opened = TRUE;
	}

	/* create ahead of time */
	firmware = dfu_firmware_new ();
	dfu_firmware_set_vid (firmware, priv->runtime_vid);
	dfu_firmware_set_pid (firmware, priv->runtime_pid);
	dfu_firmware_set_release (firmware, 0xffff);

	/* APP -> DFU */
	if (flags & DFU_TARGET_TRANSFER_FLAG_DETACH) {

		/* inform UI there's going to be a detach:attach */
		if (progress_cb != NULL) {
			progress_cb (DFU_STATE_APP_DETACH, 0, 0,
				     progress_cb_data);
		}

		/* detach and USB reset */
		target_default = dfu_device_get_target_default (device, error);
		if (target_default == NULL)
			return NULL;
		if (dfu_target_get_mode (target_default) == DFU_MODE_RUNTIME) {
			g_debug ("detaching");
			if (!dfu_target_detach (target_default, NULL, error))
				return NULL;
			if (!dfu_device_wait_for_replug (device, 5000, NULL, error))
				return NULL;
		}
	}

	/* upload from each target */
	targets = dfu_device_get_targets (device);
	for (i = 0; i < targets->len; i++) {
		DfuTarget *target;
		g_autoptr(DfuImage) image = NULL;
		target = g_ptr_array_index (targets, i);
		image = dfu_target_upload (target, 0,
					   DFU_TARGET_TRANSFER_FLAG_NONE,
					   cancellable,
					   progress_cb,
					   progress_cb_data,
					   error);
		if (image == NULL)
			return NULL;
		dfu_firmware_add_image (firmware, image);
	}

	/* choose the most appropriate type */
	if (targets->len > 1) {
		g_debug ("switching to DefuSe automatically");
		dfu_firmware_set_format (firmware, DFU_FIRMWARE_FORMAT_DFUSE);
	} else {
		dfu_firmware_set_format (firmware, DFU_FIRMWARE_FORMAT_DFU_1_0);
	}

	/* do host reset */
	if ((flags & DFU_TARGET_TRANSFER_FLAG_HOST_RESET) > 0 ||
	    (flags & DFU_TARGET_TRANSFER_FLAG_BOOT_RUNTIME) > 0) {
		if (!dfu_device_reset (device, error))
			return NULL;
	}

	/* boot to runtime */
	if (flags & DFU_TARGET_TRANSFER_FLAG_BOOT_RUNTIME) {
		g_debug ("booting to runtime");

		/* inform UI there's going to be a detach:attach */
		if (progress_cb != NULL) {
			progress_cb (DFU_STATE_APP_DETACH, 0, 0,
				     progress_cb_data);
		}

		/* DFU -> APP */
		if (!dfu_device_wait_for_replug (device, 2000, cancellable, error))
			return NULL;
	}

	/* auto-close */
	if (auto_opened) {
		if (!dfu_device_close (device, error))
			return FALSE;
	}

	/* success */
	return g_object_ref (firmware);
}

/**
 * dfu_device_download:
 * @device: a #DfuDevice
 * @firmware: a #DfuFirmware
 * @flags: flags to use, e.g. %DFU_TARGET_TRANSFER_FLAG_VERIFY
 * @cancellable: a #GCancellable, or %NULL
 * @progress_cb: a #GFileProgressCallback, or %NULL
 * @progress_cb_data: user data to pass to @progress_cb
 * @error: a #GError, or %NULL
 *
 * Downloads firmware from the host to the target, optionally verifying
 * the transfer.
 *
 * Return value: %TRUE for success
 *
 * Since: 0.5.4
 **/
gboolean
dfu_device_download (DfuDevice *device,
		     DfuFirmware *firmware,
		     DfuTargetTransferFlags flags,
		     GCancellable *cancellable,
		     DfuProgressCallback progress_cb,
		     gpointer progress_cb_data,
		     GError **error)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	GPtrArray *images;
	gboolean auto_opened = FALSE;
	guint i;
	g_autoptr(DfuTarget) target_default = NULL;
	g_autoptr(GPtrArray) targets = NULL;

	/* do we allow wildcard VID:PID matches */
	if ((flags & DFU_TARGET_TRANSFER_FLAG_WILDCARD_VID) == 0) {
		if (dfu_firmware_get_vid (firmware) == 0xffff) {
			g_set_error (error,
				     DFU_ERROR,
				     DFU_ERROR_NOT_SUPPORTED,
				     "firmware vendor ID not specified");
			return FALSE;
		}
	}
	if ((flags & DFU_TARGET_TRANSFER_FLAG_WILDCARD_PID) == 0) {
		if (dfu_firmware_get_pid (firmware) == 0xffff) {
			g_set_error (error,
				     DFU_ERROR,
				     DFU_ERROR_NOT_SUPPORTED,
				     "firmware product ID not specified");
			return FALSE;
		}
	}

	/* check vendor matches */
	if (dfu_firmware_get_vid (firmware) != 0xffff &&
	    dfu_device_get_runtime_pid (device) != 0xffff &&
	    dfu_firmware_get_vid (firmware) != dfu_device_get_runtime_vid (device)) {
		g_set_error (error,
			     DFU_ERROR,
			     DFU_ERROR_NOT_SUPPORTED,
			     "vendor ID incorrect, expected 0x%04x got 0x%04x\n",
			     dfu_firmware_get_vid (firmware),
			     dfu_device_get_runtime_vid (device));
		return FALSE;
	}

	/* check product matches */
	if (dfu_firmware_get_pid (firmware) != 0xffff &&
	    dfu_device_get_runtime_pid (device) != 0xffff &&
	    dfu_firmware_get_pid (firmware) != dfu_device_get_runtime_pid (device)) {
		g_set_error (error,
			     DFU_ERROR,
			     DFU_ERROR_NOT_SUPPORTED,
			     "product ID incorrect, expected 0x%04x got 0x%04x",
			     dfu_firmware_get_pid (firmware),
			     dfu_device_get_runtime_pid (device));
		return FALSE;
	}

	/* auto-open */
	if (!priv->device_open) {
		if (!dfu_device_open (device, error))
			return FALSE;
		auto_opened = TRUE;
	}

	/* APP -> DFU */
	if (flags & DFU_TARGET_TRANSFER_FLAG_DETACH) {

		/* inform UI there's going to be a detach:attach */
		if (progress_cb != NULL) {
			progress_cb (DFU_STATE_APP_DETACH, 0, 0,
				     progress_cb_data);
		}

		/* detach and USB reset */
		target_default = dfu_device_get_target_default (device, error);
		if (target_default == NULL)
			return FALSE;
		if (dfu_target_get_mode (target_default) == DFU_MODE_RUNTIME) {
			g_debug ("detaching");
			if (!dfu_target_detach (target_default, NULL, error))
				return FALSE;
			if (!dfu_device_wait_for_replug (device, 5000, NULL, error))
				return FALSE;
		}
	}

	/* download each target */
	images = dfu_firmware_get_images (firmware);
	if (images->len == 0) {
		g_set_error_literal (error,
				     DFU_ERROR,
				     DFU_ERROR_INVALID_FILE,
				     "no images in firmware file");
		return FALSE;
	}
	for (i = 0; i < images->len; i++) {
		DfuImage *image;
		DfuTargetTransferFlags flags_local = DFU_TARGET_TRANSFER_FLAG_NONE;
		g_autoptr(DfuTarget) target_tmp = NULL;
		image = g_ptr_array_index (images, i);
		target_tmp = dfu_device_get_target_by_alt_setting (device,
								   dfu_image_get_alt_setting (image),
								   error);
		if (target_tmp == NULL)
			return FALSE;
		if (flags & DFU_TARGET_TRANSFER_FLAG_VERIFY)
			flags_local = DFU_TARGET_TRANSFER_FLAG_VERIFY;
		if (!dfu_target_open (target_tmp, DFU_TARGET_OPEN_FLAG_NONE,
				      cancellable, error))
			return FALSE;
		if (!dfu_target_download (target_tmp,
					  image,
					  flags_local,
					  cancellable,
					  progress_cb,
					  progress_cb_data,
					  error))
			return FALSE;
	}

	/* do a host reset */
	if ((flags & DFU_TARGET_TRANSFER_FLAG_HOST_RESET) > 0 ||
	    (flags & DFU_TARGET_TRANSFER_FLAG_BOOT_RUNTIME) > 0) {
		if (!dfu_device_reset (device, error))
			return FALSE;
	}

	/* boot to runtime */
	if (flags & DFU_TARGET_TRANSFER_FLAG_BOOT_RUNTIME) {
		g_debug ("booting to runtime to set auto-boot");

		/* inform UI there's going to be a detach:attach */
		if (progress_cb != NULL) {
			progress_cb (DFU_STATE_APP_DETACH, 0, 0,
				     progress_cb_data);
		}

		/* DFU -> APP */
		if (!dfu_device_wait_for_replug (device, 2000, cancellable, error))
			return FALSE;
		target_default = dfu_device_get_target_default (device, error);
		if (target_default == NULL)
			return FALSE;
		if (!dfu_target_open (target_default,
				      DFU_TARGET_OPEN_FLAG_NONE,
				      NULL, error))
			return FALSE;
	}

	/* auto-close */
	if (auto_opened) {
		if (!dfu_device_close (device, error))
			return FALSE;
	}

	return TRUE;
}
