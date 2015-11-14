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
 * SECTION:dfu-firmware
 * @short_description: Object representing a DFU-capable target
 *
 * This object allows uploading and downloading firmware onto a
 * specific DFU-capable target.
 */

#include "config.h"

#include <fwupd.h>
#include <string.h>
#include <math.h>

#include "dfu-common.h"
#include "dfu-device-private.h"
#include "dfu-target-private.h"

static void dfu_target_finalize			 (GObject *object);

typedef enum {
	DFU_ATTRIBUTE_NONE			= 0,
	DFU_ATTRIBUTE_CAN_DOWNLOAD		= (1 << 0),
	DFU_ATTRIBUTE_CAN_UPLOAD		= (1 << 1),
	DFU_ATTRIBUTE_MANIFEST_TOL		= (1 << 2),
	DFU_ATTRIBUTE_WILL_DETACH		= (1 << 3),
	DFU_ATTRIBUTE_LAST
} DfuAttributes;

typedef enum {
	DFU_QUIRK_NONE				= 0,
	DFU_QUIRK_IGNORE_POLLTIMEOUT		= (1 << 0),
	DFU_QUIRK_FORCE_DFU_MODE		= (1 << 1),
	DFU_QUIRK_IGNORE_INVALID_VERSION	= (1 << 2),
	DFU_QUIRK_USE_PROTOCOL_ZERO		= (1 << 3),
	DFU_QUIRK_LAST
} DfuQuirks;

/**
 * DfuTargetPrivate:
 *
 * Private #DfuTarget data
 **/
typedef struct {
	DfuMode			 mode;
	DfuState		 state;
	DfuStatus		 status;
	DfuDevice		*device;
	gboolean		 interface_claimed;
	guint16			 transfer_size;
	guint8			 iface_number;
	guint8			 iface_alt_setting;
	guint8			 iface_alt_setting_idx;
	gchar			*iface_alt_setting_name;
	guint			 dnload_timeout;
	guint			 timeout_ms;
	DfuAttributes		 attributes;
	DfuQuirks		 quirks;
} DfuTargetPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (DfuTarget, dfu_target, G_TYPE_OBJECT)
#define GET_PRIVATE(o) (dfu_target_get_instance_private (o))

/**
 * dfu_target_class_init:
 **/
static void
dfu_target_class_init (DfuTargetClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = dfu_target_finalize;
}

/**
 * dfu_target_init:
 **/
static void
dfu_target_init (DfuTarget *target)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	priv->state = DFU_STATE_APP_IDLE;
	priv->status = DFU_STATUS_OK;
	priv->timeout_ms = 500;
	priv->transfer_size = 64;
}

/**
 * dfu_target_finalize:
 **/
static void
dfu_target_finalize (GObject *object)
{
	DfuTarget *target = DFU_TARGET (object);
	DfuTargetPrivate *priv = GET_PRIVATE (target);

	g_free (priv->iface_alt_setting_name);
	if (priv->device != NULL)
		g_object_unref (priv->device);

	G_OBJECT_CLASS (dfu_target_parent_class)->finalize (object);
}

typedef struct __attribute__((packed)) {
	guint8		bLength;
	guint8		bDescriptorType;
	guint8		bmAttributes;
	guint16		wDetachTimeOut;
	guint16		wTransferSize;
	guint16		bcdDFUVersion;
} DfuFuncDescriptor;

/**
 * dfu_target_get_quirks:
 **/
static DfuQuirks
dfu_target_get_quirks (GUsbDevice *dev)
{
	DfuQuirks quirks = DFU_QUIRK_NONE;
	guint16 vid, pid, release;

	vid = g_usb_device_get_vid (dev);
	pid = g_usb_device_get_pid (dev);
	release = g_usb_device_get_release (dev);

	/* Openmoko Freerunner / GTA02 */
	if ((vid == 0x1d50 || vid == 0x1457) &&
	    pid >= 0x5117 && pid <= 0x5126)
		quirks |= DFU_QUIRK_IGNORE_POLLTIMEOUT;

	/* OpenPCD Reader */
	if (vid == 0x16c0 && pid == 0x076b)
		quirks |= DFU_QUIRK_IGNORE_POLLTIMEOUT;

	/* Siemens AG, PXM 40 & PXM 50 */
	if (vid == 0x0908 && (pid == 0x02c4 || pid == 0x02c5) && release == 0x0)
		quirks |= DFU_QUIRK_IGNORE_POLLTIMEOUT;

	/* Midiman M-Audio Transit */
	if (vid == 0x0763 && pid == 0x2806)
		quirks |= DFU_QUIRK_IGNORE_POLLTIMEOUT;

	/* the LPC DFU bootloader uses the wrong mode */
	if (vid == 0x1fc9 && pid == 0x000c)
		quirks |= DFU_QUIRK_FORCE_DFU_MODE;

	/* the Leaflabs Maple3 is known broken */
	if (vid == 0x1eaf && pid == 0x0003 && release == 0x0200)
		quirks |= DFU_QUIRK_IGNORE_INVALID_VERSION;

	/* the DSO Nano has uses 0 instead of 2 when in DFU mode */
//	quirks |= DFU_QUIRK_USE_PROTOCOL_ZERO;

	return quirks;
}

/**
 * dfu_target_update_from_iface:
 **/
static gboolean
dfu_target_update_from_iface (DfuTarget *target, GUsbInterface *iface)
{
	DfuMode mode = DFU_MODE_UNKNOWN;
	DfuQuirks quirks;
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	GBytes *iface_data = NULL;
	GUsbDevice *dev;
	const DfuFuncDescriptor *desc;
	gsize iface_data_length;

	/* runtime */
	if (g_usb_interface_get_protocol (iface) == 0x01)
		mode = DFU_MODE_RUNTIME;

	/* DFU */
	if (g_usb_interface_get_protocol (iface) == 0x02)
		mode = DFU_MODE_DFU;

	/* the DSO Nano has uses 0 instead of 2 when in DFU mode */
	dev = _dfu_device_get_usb_dev (priv->device);
	quirks = dfu_target_get_quirks (dev);
	if ((quirks & DFU_QUIRK_USE_PROTOCOL_ZERO) &&
	    g_usb_interface_get_protocol (iface) == 0x00)
		mode = DFU_MODE_DFU;

	/* nothing found */
	if (mode == DFU_MODE_UNKNOWN)
		return FALSE;

	/* in DFU mode, the interface is supposed to be 0 */
	if (mode == DFU_MODE_DFU && g_usb_interface_get_number (iface) != 0)
		g_warning ("iface has to be 0 in DFU mode, got 0x%02i",
			   g_usb_interface_get_number (iface));

	/* some devices set the wrong mode */
	if (quirks & DFU_QUIRK_FORCE_DFU_MODE)
		mode = DFU_MODE_DFU;

	/* save for reset */
	if (mode == DFU_MODE_RUNTIME) {
		_dfu_device_set_runtime_vid (priv->device, g_usb_device_get_vid (dev));
		_dfu_device_set_runtime_pid (priv->device, g_usb_device_get_pid (dev));
	}

	/* update */
	priv->iface_number = g_usb_interface_get_number (iface);
	priv->iface_alt_setting = g_usb_interface_get_alternate (iface);
	priv->iface_alt_setting_idx = g_usb_interface_get_index (iface);
	priv->quirks = quirks;
	priv->mode = mode;

	/* parse the functional descriptor */
	iface_data = g_usb_interface_get_extra (iface);
	desc = g_bytes_get_data (iface_data, &iface_data_length);
	if (iface_data_length != 0x09) {
		g_warning ("interface found, but no interface data");
		return FALSE;
	}

	/* check sanity */
	if (desc->bLength != 0x09) {
		g_warning ("DFU interface data has incorrect length: 0x%02x",
			   desc->bLength);
	}

	/* check transfer size */
	priv->transfer_size = desc->wTransferSize;
	if (priv->transfer_size == 0x0000) {
		g_warning ("DFU transfer size invalid, using default: 0x%04x",
			   desc->wTransferSize);
		priv->transfer_size = 64;
	}

	/* check DFU version */
	if (quirks & DFU_QUIRK_IGNORE_INVALID_VERSION) {
		g_debug ("ignoring quirked DFU version");
	} else {
		if (desc->bcdDFUVersion != 0x0100 &&
		    desc->bcdDFUVersion != 0x0101) {
			g_warning ("DFU version is invalid: 0x%04x",
				   desc->bcdDFUVersion);
		}
	}

	/* get attributes about the DFU operation */
	priv->attributes = desc->bmAttributes;
	return TRUE;
}

/**
 * _dfu_target_new:
 * @device: a #DfuDevice
 * @iface: a #GUsbInterface
 *
 * Creates a new DFU target, which represents an alt-setting on a
 * DFU-capable device.
 *
 * Return value: a #DfuTarget, or %NULL if @iface was not DFU-capable
 *
 * Since: 0.5.4
 **/
DfuTarget *
_dfu_target_new (DfuDevice *device, GUsbInterface *iface)
{
	DfuTargetPrivate *priv;
	DfuTarget *target;
	target = g_object_new (DFU_TYPE_TARGET, NULL);
	priv = GET_PRIVATE (target);
	priv->device = g_object_ref (device);
	if (!dfu_target_update_from_iface (target, iface)) {
		g_object_unref (target);
		return NULL;
	}
	return target;
}

/**
 * dfu_target_get_mode:
 * @target: a #GUsbDevice
 *
 * Gets the target mode.
 *
 * Return value: enumerated mode, e.g. %DFU_MODE_RUNTIME
 *
 * Since: 0.5.4
 **/
DfuMode
dfu_target_get_mode (DfuTarget *target)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	g_return_val_if_fail (DFU_IS_TARGET (target), 0);
	return priv->mode;
}

/**
 * dfu_target_get_state:
 * @target: a #GUsbDevice
 *
 * Gets the target state.
 *
 * Return value: enumerated state, e.g. %DFU_STATE_DFU_UPLOAD_IDLE
 *
 * Since: 0.5.4
 **/
DfuState
dfu_target_get_state (DfuTarget *target)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	g_return_val_if_fail (DFU_IS_TARGET (target), 0);
	return priv->state;
}

/**
 * dfu_target_get_status:
 * @target: a #GUsbDevice
 *
 * Gets the target status.
 *
 * Return value: enumerated status, e.g. %DFU_STATUS_ERR_ADDRESS
 *
 * Since: 0.5.4
 **/
DfuStatus
dfu_target_get_status (DfuTarget *target)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	g_return_val_if_fail (DFU_IS_TARGET (target), 0);
	return priv->status;
}

/**
 * dfu_target_can_upload:
 * @target: a #GUsbDevice
 *
 * Gets if the target can upload.
 *
 * Return value: %TRUE if the target can upload from target to host
 *
 * Since: 0.5.4
 **/
gboolean
dfu_target_can_upload (DfuTarget *target)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	g_return_val_if_fail (DFU_IS_TARGET (target), FALSE);
	return (priv->attributes & DFU_ATTRIBUTE_CAN_UPLOAD) > 0;
}

/**
 * dfu_target_can_download:
 * @target: a #GUsbDevice
 *
 * Gets if the target can download.
 *
 * Return value: %TRUE if the target can download from host to target
 *
 * Since: 0.5.4
 **/
gboolean
dfu_target_can_download (DfuTarget *target)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	g_return_val_if_fail (DFU_IS_TARGET (target), FALSE);
	return (priv->attributes & DFU_ATTRIBUTE_CAN_DOWNLOAD) > 0;
}

/**
 * dfu_target_get_transfer_size:
 * @target: a #GUsbDevice
 *
 * Gets the transfer size in bytes.
 *
 * Return value: packet size, or 0 for unknown
 *
 * Since: 0.5.4
 **/
guint16
dfu_target_get_transfer_size (DfuTarget *target)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	g_return_val_if_fail (DFU_IS_TARGET (target), 0xffff);
	return priv->transfer_size;
}

/**
 * dfu_target_set_transfer_size:
 * @target: a #GUsbDevice
 * @transfer_size: maximum packet size
 *
 * Sets the transfer size in bytes.
 *
 * Since: 0.5.4
 **/
void
dfu_target_set_transfer_size (DfuTarget *target, guint16 transfer_size)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	g_return_if_fail (DFU_IS_TARGET (target));
	priv->transfer_size = transfer_size;
}

/**
 * dfu_target_open:
 * @target: a #DfuTarget
 * @flags: #DfuTargetOpenFlags, e.g. %DFU_TARGET_OPEN_FLAG_NONE
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Opens a DFU-capable target.
 *
 * Return value: %TRUE for success
 *
 * Since: 0.5.4
 **/
gboolean
dfu_target_open (DfuTarget *target, DfuTargetOpenFlags flags,
		 GCancellable *cancellable, GError **error)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	GUsbDevice *dev;
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail (DFU_IS_TARGET (target), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* already done */
	if (priv->interface_claimed)
		return TRUE;

	/* ensure parent device is open */
	if (!dfu_device_open (priv->device, error))
		return FALSE;

	/* claim the correct interface */
	dev = _dfu_device_get_usb_dev (priv->device);
	if (!g_usb_device_claim_interface (dev, (gint) priv->iface_number, 0, &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "cannot claim interface %i: %s",
			     priv->iface_number, error_local->message);
		return FALSE;
	}
	priv->interface_claimed = TRUE;

	/* use the correct setting */
	if (priv->mode == DFU_MODE_DFU) {
		if (!g_usb_device_set_interface_alt (dev,
						     (gint) priv->iface_number,
						     (gint) priv->iface_alt_setting,
						     &error_local)) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "cannot set alternate setting 0x%02x on interface %i: %s",
				     priv->iface_alt_setting,
				     priv->iface_number,
				     error_local->message);
			return FALSE;
		}
	}

	/* get string */
	if (priv->iface_alt_setting_idx != 0x00) {
		priv->iface_alt_setting_name =
			g_usb_device_get_string_descriptor (dev,
							    priv->iface_alt_setting_idx,
							    NULL);
	}

	/* automatically abort any uploads or downloads */
	if ((flags & DFU_TARGET_OPEN_FLAG_NO_AUTO_REFRESH) == 0) {
		if (!dfu_target_refresh (target, cancellable, error))
			return FALSE;
		switch (priv->state) {
		case DFU_STATE_DFU_UPLOAD_IDLE:
		case DFU_STATE_DFU_DNLOAD_IDLE:
		case DFU_STATE_DFU_DNLOAD_SYNC:
			g_debug ("aborting transfer %s", dfu_status_to_string (priv->status));
			if (!dfu_target_abort (target, cancellable, error))
				return FALSE;
			break;
		case DFU_STATE_DFU_ERROR:
			g_debug ("clearing error %s", dfu_status_to_string (priv->status));
			if (!dfu_target_clear_status (target, cancellable, error))
				return FALSE;
			break;
		default:
			break;
		}
	}

	return TRUE;
}

/**
 * dfu_target_close:
 * @target: a #DfuTarget
 * @error: a #GError, or %NULL
 *
 * Closes a DFU-capable target.
 *
 * Return value: %TRUE for success
 *
 * Since: 0.5.4
 **/
gboolean
dfu_target_close (DfuTarget *target, GError **error)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	GUsbDevice *dev;
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail (DFU_IS_TARGET (target), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* this is our intention; the release might fail if the USB device
	 * has been disconnected already */
	priv->interface_claimed = FALSE;

	/* only release if claimed */
	if (priv->interface_claimed) {
		dev = _dfu_device_get_usb_dev (priv->device);
		if (!g_usb_device_release_interface (dev,
						     (gint) priv->iface_number,
						     0,
						     &error_local)) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "cannot release interface %i: %s",
				     priv->iface_number, error_local->message);
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

/**
 * dfu_target_refresh:
 * @target: a #DfuTarget
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Refreshes the cached properties on the DFU target.
 *
 * Return value: %TRUE for success
 *
 * Since: 0.5.4
 **/
gboolean
dfu_target_refresh (DfuTarget *target, GCancellable *cancellable, GError **error)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	gsize actual_length = 0;
	guint8 buf[6];
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail (DFU_IS_TARGET (target), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (!g_usb_device_control_transfer (_dfu_device_get_usb_dev (priv->device),
					    G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
					    G_USB_DEVICE_REQUEST_TYPE_CLASS,
					    G_USB_DEVICE_RECIPIENT_INTERFACE,
					    DFU_REQUEST_GETSTATUS,
					    0,
					    priv->iface_number,
					    buf, sizeof(buf), &actual_length,
					    priv->timeout_ms,
					    cancellable,
					    &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "cannot get target state: %s",
			     error_local->message);
		return FALSE;
	}
	if (actual_length != 6) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "cannot get target status, invalid size: %04x",
			     (guint) actual_length);
	}
	priv->status = buf[0];
	if (priv->quirks & DFU_QUIRK_IGNORE_POLLTIMEOUT) {
		priv->dnload_timeout = 5;
	} else {
		priv->dnload_timeout = buf[1] +
					(((guint32) buf[2]) << 8) +
					(((guint32) buf[3]) << 16);
	}
	priv->state = buf[4];
	g_debug ("refreshed status=%s and state=%s",
		 dfu_status_to_string (priv->status),
		 dfu_state_to_string (priv->state));
	return TRUE;
}

/**
 * dfu_target_detach:
 * @target: a #DfuTarget
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Detaches the target putting it into DFU-mode.
 *
 * Return value: %TRUE for success
 *
 * Since: 0.5.4
 **/
gboolean
dfu_target_detach (DfuTarget *target, GCancellable *cancellable, GError **error)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail (DFU_IS_TARGET (target), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (!g_usb_device_control_transfer (_dfu_device_get_usb_dev (priv->device),
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_CLASS,
					    G_USB_DEVICE_RECIPIENT_INTERFACE,
					    DFU_REQUEST_DETACH,
					    0,
					    priv->iface_number,
					    NULL, 0, NULL,
					    priv->timeout_ms,
					    cancellable,
					    &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "cannot detach target: %s",
			     error_local->message);
		return FALSE;
	}

	/* do a host reset */
	if ((priv->attributes & DFU_ATTRIBUTE_WILL_DETACH) == 0) {
		g_debug ("doing target reset as host will not self-reset");
		if (!dfu_device_reset (priv->device, error))
			return FALSE;
	}
	return TRUE;
}

/**
 * dfu_target_abort:
 * @target: a #DfuTarget
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Aborts any upload or download in progress.
 *
 * Return value: %TRUE for success
 *
 * Since: 0.5.4
 **/
gboolean
dfu_target_abort (DfuTarget *target, GCancellable *cancellable, GError **error)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail (DFU_IS_TARGET (target), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (!g_usb_device_control_transfer (_dfu_device_get_usb_dev (priv->device),
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_CLASS,
					    G_USB_DEVICE_RECIPIENT_INTERFACE,
					    DFU_REQUEST_ABORT,
					    0,
					    priv->iface_number,
					    NULL, 0, NULL,
					    priv->timeout_ms,
					    cancellable,
					    &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "cannot abort target: %s",
			     error_local->message);
		return FALSE;
	}
	return TRUE;
}

/**
 * _dfu_target_update:
 * @target: a #DfuTarget
 * @iface: a #GUsbInterface
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Updates the target with new interface data. This only needs to be
 * done after the device has been reset.
 * 
 * Returns: %TRUE for success
 **/
gboolean
_dfu_target_update (DfuTarget *target, GUsbInterface *iface,
		    GCancellable *cancellable, GError **error)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	gboolean reclaim_interface = FALSE;

	/* close */
	if (priv->interface_claimed) {
		if (!dfu_target_close (target, error))
			return FALSE;
		reclaim_interface = TRUE;
	}

	/* check this is _still_ a DFU-capable target */
	if (!dfu_target_update_from_iface (target, iface)) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "replugged target is not DFU-capable");
		return FALSE;
	}

	/* reclaim */
	if (reclaim_interface) {
		if (!dfu_device_open (priv->device, error))
			return FALSE;
		if (!dfu_target_open (target, DFU_TARGET_OPEN_FLAG_NONE,
				      cancellable, error))
			return FALSE;
	}
	return TRUE;
}

/**
 * dfu_target_clear_status:
 * @target: a #DfuTarget
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Clears any error status on the DFU target.
 *
 * Return value: %TRUE for success
 *
 * Since: 0.5.4
 **/
gboolean
dfu_target_clear_status (DfuTarget *target, GCancellable *cancellable, GError **error)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail (DFU_IS_TARGET (target), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (!g_usb_device_control_transfer (_dfu_device_get_usb_dev (priv->device),
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_CLASS,
					    G_USB_DEVICE_RECIPIENT_INTERFACE,
					    DFU_REQUEST_CLRSTATUS,
					    0,
					    priv->iface_number,
					    NULL, 0, NULL,
					    priv->timeout_ms,
					    cancellable,
					    &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "cannot clear status on the target: %s",
			     error_local->message);
		return FALSE;
	}
	return TRUE;
}

/**
 * dfu_target_upload_chunk:
 **/
static GBytes *
dfu_target_upload_chunk (DfuTarget *target, guint8 index,
			 GCancellable *cancellable, GError **error)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	g_autoptr(GError) error_local = NULL;
	guint8 *buf;
	gsize actual_length;

	buf = g_new0 (guint8, priv->transfer_size);
	if (!g_usb_device_control_transfer (_dfu_device_get_usb_dev (priv->device),
					    G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
					    G_USB_DEVICE_REQUEST_TYPE_CLASS,
					    G_USB_DEVICE_RECIPIENT_INTERFACE,
					    DFU_REQUEST_UPLOAD,
					    index,
					    priv->iface_number,
					    buf, (gsize) priv->transfer_size,
					    &actual_length,
					    priv->timeout_ms,
					    cancellable,
					    &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "cannot clear status on the target: %s",
			     error_local->message);
		return NULL;
	}
	return g_bytes_new_take (buf, actual_length);
}

/**
 * dfu_target_upload:
 * @target: a #DfuTarget
 * @flags: flags to use, e.g. %DFU_TARGET_TRANSFER_FLAG_VERIFY
 * @expected_size: the expected size of the firmware, or 0 for unknown
 * @cancellable: a #GCancellable, or %NULL
 * @progress_cb: a #GFileProgressCallback, or %NULL
 * @progress_cb_data: user data to pass to @progress_cb
 * @error: a #GError, or %NULL
 *
 * Uploads firmware from the target to the host.
 *
 * Return value: (transfer full): the uploaded image, or %NULL for error
 *
 * Since: 0.5.4
 **/
DfuImage *
dfu_target_upload (DfuTarget *target,
		   gsize expected_size,
		   DfuTargetTransferFlags flags,
		   GCancellable *cancellable,
		   DfuProgressCallback progress_cb,
		   gpointer progress_cb_data,
		   GError **error)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	DfuImage *image = NULL;
	GBytes *chunk_tmp;
	gsize chunk_size;
	gsize offset = 0;
	gsize total_size = 0;
	guint8 *buffer;
	guint i;
	g_autoptr(DfuElement) element = NULL;
	g_autoptr(GBytes) contents = NULL;
	g_autoptr(GPtrArray) chunks = NULL;

	g_return_val_if_fail (DFU_IS_TARGET (target), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* can the target do this? */
	if (!dfu_target_can_upload (target)) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "target cannot do uploading");
		return NULL;
	}

	/* get all the chunks from the hardware */
	chunks = g_ptr_array_new_with_free_func ((GDestroyNotify) g_bytes_unref);
	for (i = 0; i < 0xffff; i++) {
		chunk_tmp = dfu_target_upload_chunk (target, i, cancellable, error);
		if (chunk_tmp == NULL)
			return NULL;

		/* keep a sum of all the chunks */
		chunk_size = g_bytes_get_size (chunk_tmp);
		total_size += chunk_size;

		/* add to array */
		g_debug ("got #%04x chunk of size %li", i, chunk_size);
		g_ptr_array_add (chunks, chunk_tmp);

		/* update UI */
		if (progress_cb != NULL && chunk_size > 0) {
			progress_cb (DFU_STATE_DFU_UPLOAD_IDLE,
				     total_size,
				     expected_size,
				     progress_cb_data);
		}

		/* detect short write as EOF */
		if (chunk_size < priv->transfer_size)
			break;
	}

	/* check final size */
	if (expected_size > 0) {
		if (total_size != expected_size) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "invalid size, got %li, expected %li",
				     total_size, expected_size);
			return NULL;
		}
	}

	/* do host reset */
	if ((flags & DFU_TARGET_TRANSFER_FLAG_HOST_RESET) > 0 ||
	    (flags & DFU_TARGET_TRANSFER_FLAG_BOOT_RUNTIME) > 0) {
		if (!dfu_device_reset (priv->device, error))
			return NULL;
	}

	/* boot to runtime */
	if (flags & DFU_TARGET_TRANSFER_FLAG_BOOT_RUNTIME) {
		g_debug ("booting to runtime");
		if (!dfu_device_wait_for_replug (priv->device, 2000, cancellable, error))
			return NULL;
	}

	/* stitch them all together */
	buffer = g_malloc0 (total_size);
	for (i = 0; i < chunks->len; i++) {
		const guint8 *chunk_data;
		chunk_tmp = g_ptr_array_index (chunks, i);
		chunk_data = g_bytes_get_data (chunk_tmp, &chunk_size);
		memcpy (buffer + offset, chunk_data, chunk_size);
		offset += chunk_size;
	}

	/* create new image */
	contents = g_bytes_new_take (buffer, total_size);
	image = dfu_image_new ();
	dfu_image_set_name (image, priv->iface_alt_setting_name);
	dfu_image_set_alt_setting (image, priv->iface_alt_setting);
	element = dfu_element_new ();
	dfu_element_set_contents (element, contents);
	dfu_image_add_element (image, element);
	return image;
}

/**
 * dfu_target_download_chunk:
 **/
static gboolean
dfu_target_download_chunk (DfuTarget *target, guint8 index, GBytes *bytes,
			   GCancellable *cancellable, GError **error)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	g_autoptr(GError) error_local = NULL;
	gsize actual_length;

	if (!g_usb_device_control_transfer (_dfu_device_get_usb_dev (priv->device),
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_CLASS,
					    G_USB_DEVICE_RECIPIENT_INTERFACE,
					    DFU_REQUEST_DNLOAD,
					    index,
					    priv->iface_number,
					    (guint8 *) g_bytes_get_data (bytes, NULL),
					    g_bytes_get_size (bytes),
					    &actual_length,
					    priv->timeout_ms,
					    cancellable,
					    &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "cannot download data to the target (state: %s): %s",
			     dfu_state_to_string (priv->state),
			     error_local->message);
		return FALSE;
	}
	g_assert (actual_length == g_bytes_get_size (bytes));
	return TRUE;
}

/**
 * _g_bytes_compare_verbose:
 **/
static gchar *
_g_bytes_compare_verbose (GBytes *bytes1, GBytes *bytes2)
{
	const guint8 *data1;
	const guint8 *data2;
	gsize length1;
	gsize length2;
	guint i;

	data1 = g_bytes_get_data (bytes1, &length1);
	data2 = g_bytes_get_data (bytes2, &length2);

	/* not the same length */
	if (length1 != length2) {
		return g_strdup_printf ("got %li bytes, expected %li",
					length1, length2);
	}

	/* return 00 01 02 03 */
	for (i = 0; i < length1; i++) {
		if (data1[i] != data2[i]) {
			return g_strdup_printf ("got 0x%02x, expected 0x%02x @ 0x%04x",
						data1[i], data2[i], i);
		}
	}
	return NULL;
}

/**
 * dfu_target_download_bytes:
 **/
static gboolean
dfu_target_download_bytes (DfuTarget *target, GBytes *bytes,
			   DfuTargetTransferFlags flags,
			   GCancellable *cancellable,
			   DfuProgressCallback progress_cb,
			   gpointer progress_cb_data,
			   GError **error)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	DfuElement *element;
	guint i;
	guint nr_chunks;
	g_autoptr(GError) error_local = NULL;

	/* can the target do this? */
	if (!dfu_target_can_download (target)) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "target cannot do downloading");
		return FALSE;
	}

	/* round up as we have to transfer incomplete blocks */
	nr_chunks = ceil ((gdouble) g_bytes_get_size (bytes) /
			  (gdouble) priv->transfer_size);
	if (nr_chunks == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOTHING_TO_DO,
				     "zero-length firmware");
		return FALSE;
	}
	for (i = 0; i < nr_chunks + 1; i++) {
		gsize length;
		gsize offset;
		g_autoptr(GBytes) bytes_tmp = NULL;

		/* we have to write one final zero-sized chunk for EOF */
		offset = i * priv->transfer_size;
		if (i < nr_chunks) {
			length = g_bytes_get_size (bytes) - offset;
			if (length > priv->transfer_size)
				length = priv->transfer_size;
			bytes_tmp = g_bytes_new_from_bytes (bytes, offset, length);
		} else {
			bytes_tmp = g_bytes_new (NULL, 0);
		}
		g_debug ("writing #%04x chunk of size %li",
			 i, g_bytes_get_size (bytes_tmp));
		if (!dfu_target_download_chunk (target, i, bytes_tmp, cancellable, error)) {
			if (dfu_target_refresh (target, cancellable, NULL)) {
				g_prefix_error (error, "Device status %s: ",
						dfu_status_to_string (priv->status));
			}
			return FALSE;
		}

		/* update UI */
		if (progress_cb != NULL) {
			progress_cb (DFU_STATE_DFU_DNLOAD_IDLE,
				     offset,
				     g_bytes_get_size (bytes),
				     progress_cb_data);
		}

		/* give the target a chance to update */
		g_usleep (priv->dnload_timeout * 1000);

		/* getting the status moves the state machine to DNLOAD-IDLE */
		if (!dfu_target_refresh (target, cancellable, error))
			return FALSE;
	}

	/* verify */
	if (flags & DFU_TARGET_TRANSFER_FLAG_VERIFY) {
		GBytes *bytes_tmp;
		g_autoptr(DfuImage) image_tmp = NULL;
		image_tmp = dfu_target_upload (target,
					       g_bytes_get_size (bytes),
					       DFU_TARGET_TRANSFER_FLAG_NONE,
					       cancellable,
					       progress_cb,
					       progress_cb_data,
					       error);
		if (image_tmp == NULL)
			return FALSE;
		element = dfu_image_get_element (image_tmp, 0);
		bytes_tmp = dfu_element_get_contents (element);
		if (g_bytes_compare (bytes_tmp, bytes) != 0) {
			g_autofree gchar *bytes_cmp_str = NULL;
			bytes_cmp_str = _g_bytes_compare_verbose (bytes_tmp, bytes);
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_WRITE,
				     "verify failed: %s",
				     bytes_cmp_str);
			return FALSE;
		}
	}

	/* do a host reset */
	if ((flags & DFU_TARGET_TRANSFER_FLAG_HOST_RESET) > 0 ||
	    (flags & DFU_TARGET_TRANSFER_FLAG_BOOT_RUNTIME) > 0) {
		if (!dfu_device_reset (priv->device, error))
			return FALSE;
	}

	/* boot to runtime */
	if (flags & DFU_TARGET_TRANSFER_FLAG_BOOT_RUNTIME) {
		g_debug ("booting to runtime to set auto-boot");
		if (!dfu_device_wait_for_replug (priv->device, 2000, cancellable, error))
			return FALSE;
	}

	return TRUE;
}

/**
 * dfu_target_download:
 * @target: a #DfuTarget
 * @image: a #DfuImage
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
dfu_target_download (DfuTarget *target, DfuImage *image,
		     DfuTargetTransferFlags flags,
		     GCancellable *cancellable,
		     DfuProgressCallback progress_cb,
		     gpointer progress_cb_data,
		     GError **error)
{
	GBytes *contents;
	DfuElement *element;

	g_return_val_if_fail (DFU_IS_TARGET (target), FALSE);
	g_return_val_if_fail (DFU_IS_IMAGE (image), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* get data */
	element = dfu_image_get_element (image, 0);
	if (element == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_WRITE,
				     "no image elements");
		return FALSE;
	}
	contents = dfu_element_get_contents (element);
	return dfu_target_download_bytes (target, contents, flags, cancellable,
					  progress_cb, progress_cb_data, error);
}

/**
 * dfu_target_set_timeout:
 * @target: a #DfuTarget
 * @timeout_ms: the timeout in ms
 *
 * Sets the USB timeout to use when contacting the USB target.
 *
 * Since: 0.5.4
 **/
void
dfu_target_set_timeout (DfuTarget *target, guint timeout_ms)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	g_return_if_fail (DFU_IS_TARGET (target));
	priv->timeout_ms = timeout_ms;
}

/**
 * dfu_target_get_interface_number:
 * @target: a #DfuTarget
 *
 * Gets the interface number.
 *
 * Since: 0.5.4
 **/
guint8
dfu_target_get_interface_number (DfuTarget *target)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	g_return_val_if_fail (DFU_IS_TARGET (target), 0xff);
	return priv->iface_number;
}

/**
 * dfu_target_get_interface_alt_setting:
 * @target: a #DfuTarget
 *
 * Gets the alternate setting to use for this interface.
 *
 * Return value: the alternative setting, typically zero
 *
 * Since: 0.5.4
 **/
guint8
dfu_target_get_interface_alt_setting (DfuTarget *target)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	g_return_val_if_fail (DFU_IS_TARGET (target), 0xff);
	return priv->iface_alt_setting;
}

/**
 * dfu_target_get_interface_alt_name:
 * @target: a #DfuTarget
 *
 * Gets the alternate setting name to use for this interface.
 *
 * Return value: the alternative setting name, typically %NULL
 *
 * Since: 0.5.4
 **/
const gchar *
dfu_target_get_interface_alt_name (DfuTarget *target)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	g_return_val_if_fail (DFU_IS_TARGET (target), NULL);
	return priv->iface_alt_setting_name;
}
