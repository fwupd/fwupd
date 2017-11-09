/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015-2017 Richard Hughes <richard@hughsie.com>
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
#include "dfu-target-avr.h"
#include "dfu-target-private.h"
#include "dfu-target-stm.h"

#include "fu-device-locker.h"

#include "fwupd-error.h"

static void dfu_device_finalize			 (GObject *object);

typedef struct {
	DfuDeviceAttributes	 attributes;
	DfuDeviceQuirks		 quirks;
	DfuMode			 mode;
	DfuState		 state;
	DfuStatus		 status;
	FwupdStatus		 action_last;
	GPtrArray		*targets;
	GUsbDevice		*dev;
	FuDeviceLocker		*dev_locker;
	FuQuirks		*system_quirks;
	gboolean		 open_new_dev;		/* if set new GUsbDevice */
	gboolean		 done_upload_or_download;
	gboolean		 claimed_interface;
	gchar			*chip_id;
	gchar			*display_name;
	gchar			*serial_number;
	gchar			*platform_id;
	guint16			 version;
	guint16			 runtime_pid;
	guint16			 runtime_vid;
	guint16			 runtime_release;
	guint16			 transfer_size;
	guint8			 iface_number;
	guint			 dnload_timeout;
	guint			 timeout_ms;
} DfuDevicePrivate;

enum {
	SIGNAL_STATUS_CHANGED,
	SIGNAL_STATE_CHANGED,
	SIGNAL_PERCENTAGE_CHANGED,
	SIGNAL_ACTION_CHANGED,
	SIGNAL_LAST
};

static guint signals [SIGNAL_LAST] = { 0 };

G_DEFINE_TYPE_WITH_PRIVATE (DfuDevice, dfu_device, G_TYPE_OBJECT)
#define GET_PRIVATE(o) (dfu_device_get_instance_private (o))

static void
dfu_device_class_init (DfuDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	/**
	 * DfuDevice::status-changed:
	 * @device: the #DfuDevice instance that emitted the signal
	 * @status: the new #DfuStatus
	 *
	 * The ::status-changed signal is emitted when the status changes.
	 **/
	signals [SIGNAL_STATUS_CHANGED] =
		g_signal_new ("status-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (DfuDeviceClass, status_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);

	/**
	 * DfuDevice::state-changed:
	 * @device: the #DfuDevice instance that emitted the signal
	 * @state: the new #DfuState
	 *
	 * The ::state-changed signal is emitted when the state changes.
	 **/
	signals [SIGNAL_STATE_CHANGED] =
		g_signal_new ("state-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (DfuDeviceClass, state_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);

	/**
	 * DfuDevice::percentage-changed:
	 * @device: the #DfuDevice instance that emitted the signal
	 * @percentage: the new percentage
	 *
	 * The ::percentage-changed signal is emitted when the percentage changes.
	 **/
	signals [SIGNAL_PERCENTAGE_CHANGED] =
		g_signal_new ("percentage-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (DfuDeviceClass, percentage_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);

	/**
	 * DfuDevice::action-changed:
	 * @device: the #DfuDevice instance that emitted the signal
	 * @action: the new #FwupdStatus
	 *
	 * The ::action-changed signal is emitted when the high level action changes.
	 **/
	signals [SIGNAL_ACTION_CHANGED] =
		g_signal_new ("action-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (DfuDeviceClass, action_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);

	object_class->finalize = dfu_device_finalize;
}

static void
dfu_device_init (DfuDevice *device)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	priv->iface_number = 0xff;
	priv->runtime_pid = 0xffff;
	priv->runtime_vid = 0xffff;
	priv->runtime_release = 0xffff;
	priv->state = DFU_STATE_APP_IDLE;
	priv->status = DFU_STATUS_OK;
	priv->targets = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	priv->timeout_ms = 1500;
	priv->transfer_size = 64;
}

static void
dfu_device_set_action (DfuDevice *device, FwupdStatus action)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	if (action == priv->action_last)
		return;
	g_signal_emit (device, signals[SIGNAL_ACTION_CHANGED], 0, action);
	priv->action_last = action;
}

/**
 * dfu_device_get_transfer_size:
 * @device: a #GUsbDevice
 *
 * Gets the transfer size in bytes.
 *
 * Return value: packet size, or 0 for unknown
 **/
guint16
dfu_device_get_transfer_size (DfuDevice *device)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	g_return_val_if_fail (DFU_IS_DEVICE (device), 0xffff);
	return priv->transfer_size;
}

/**
 * dfu_device_get_version:
 * @device: a #GUsbDevice
 *
 * Gets the DFU specification version supported by the device.
 *
 * Return value: integer, or 0 for unknown, e.g. %DFU_VERSION_DFU_1_1
 **/
guint16
dfu_device_get_version (DfuDevice *device)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	g_return_val_if_fail (DFU_IS_DEVICE (device), 0xffff);
	return priv->version;
}

/**
 * dfu_device_get_download_timeout:
 * @device: a #GUsbDevice
 *
 * Gets the download timeout in ms.
 *
 * Return value: delay, or 0 for unknown
 **/
guint
dfu_device_get_download_timeout (DfuDevice *device)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	g_return_val_if_fail (DFU_IS_DEVICE (device), 0);
	return priv->dnload_timeout;
}

/**
 * dfu_device_set_transfer_size:
 * @device: a #GUsbDevice
 * @transfer_size: maximum packet size
 *
 * Sets the transfer size in bytes.
 **/
void
dfu_device_set_transfer_size (DfuDevice *device, guint16 transfer_size)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	g_return_if_fail (DFU_IS_DEVICE (device));
	priv->transfer_size = transfer_size;
}

static void
dfu_device_finalize (GObject *object)
{
	DfuDevice *device = DFU_DEVICE (object);
	DfuDevicePrivate *priv = GET_PRIVATE (device);

	if (priv->dev_locker != NULL)
		g_object_unref (priv->dev_locker);
	if (priv->dev != NULL)
		g_object_unref (priv->dev);
	if (priv->system_quirks != NULL)
		g_object_unref (priv->system_quirks);
	g_free (priv->chip_id);
	g_free (priv->display_name);
	g_free (priv->serial_number);
	g_free (priv->platform_id);
	g_ptr_array_unref (priv->targets);

	G_OBJECT_CLASS (dfu_device_parent_class)->finalize (object);
}

typedef struct __attribute__((packed)) {
	guint8		bLength;
	guint8		bDescriptorType;
	guint8		bmAttributes;
	guint16		wDetachTimeOut;
	guint16		wTransferSize;
	guint16		bcdDFUVersion;
} DfuFuncDescriptor;

static gboolean
dfu_device_parse_iface_data (DfuDevice *device, GBytes *iface_data, GError **error)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	DfuFuncDescriptor desc;
	const guint8 *buf;
	gsize sz;

	/* parse the functional descriptor */
	buf = g_bytes_get_data (iface_data, &sz);
	if (sz == sizeof(DfuFuncDescriptor)) {
		memcpy (&desc, buf, sz);
	} else if (sz == sizeof(DfuFuncDescriptor) - 2) {
		g_warning ("truncated DFU interface data, no bcdDFUVersion");
		memcpy (&desc, buf, sz);
		desc.bcdDFUVersion = DFU_VERSION_DFU_1_1;
	} else {
		g_autoptr(GString) bufstr = g_string_new (NULL);
		for (gsize i = 0; i < sz; i++)
			g_string_append_printf (bufstr, "%02x ", buf[i]);
		if (bufstr->len > 0)
			g_string_truncate (bufstr, bufstr->len - 1);
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "interface found, but not the correct length for "
			     "functional data: %" G_GSIZE_FORMAT " bytes: %s",
			     sz, bufstr->str);
		return FALSE;
	}

	/* check sanity */
	if (desc.bLength != sz) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "DFU interface data has incorrect length: 0x%02x",
			     desc.bLength);
		return FALSE;
	}

	/* get transfer size and version */
	priv->transfer_size = GUINT16_FROM_LE (desc.wTransferSize);
	priv->version = GUINT16_FROM_LE (desc.bcdDFUVersion);

	/* ST-specific */
	if (priv->version == DFU_VERSION_DFUSE &&
	    desc.bmAttributes & DFU_DEVICE_ATTRIBUTE_CAN_ACCELERATE)
		priv->transfer_size = 0x1000;

	/* get attributes about the DFU operation */
	priv->attributes = desc.bmAttributes;
	return TRUE;
}

static gboolean
dfu_device_update_from_iface (DfuDevice *device, GUsbInterface *iface)
{
	DfuMode target_mode = DFU_MODE_UNKNOWN;
	DfuDevicePrivate *priv = GET_PRIVATE (device);

	/* runtime */
	if (g_usb_interface_get_protocol (iface) == 0x01)
		target_mode = DFU_MODE_RUNTIME;

	/* DFU */
	if (g_usb_interface_get_protocol (iface) == 0x02)
		target_mode = DFU_MODE_DFU;

	/* the DSO Nano has uses 0 instead of 2 when in DFU target_mode */
	if (dfu_device_has_quirk (device, DFU_DEVICE_QUIRK_USE_PROTOCOL_ZERO) &&
	    g_usb_interface_get_protocol (iface) == 0x00)
		target_mode = DFU_MODE_DFU;

	/* nothing found */
	if (target_mode == DFU_MODE_UNKNOWN)
		return FALSE;

	/* in DFU mode, the interface is supposed to be 0 */
	if (target_mode == DFU_MODE_DFU && g_usb_interface_get_number (iface) != 0)
		g_warning ("iface has to be 0 in DFU mode, got 0x%02i",
			   g_usb_interface_get_number (iface));

	/* some devices set the wrong mode */
	if (dfu_device_has_quirk (device, DFU_DEVICE_QUIRK_FORCE_DFU_MODE))
		target_mode = DFU_MODE_DFU;

	/* save for reset */
	if (target_mode == DFU_MODE_RUNTIME ||
	    (priv->quirks & DFU_DEVICE_QUIRK_NO_PID_CHANGE)) {
		priv->runtime_vid = g_usb_device_get_vid (priv->dev);
		priv->runtime_pid = g_usb_device_get_pid (priv->dev);
		priv->runtime_release = g_usb_device_get_release (priv->dev);
	}

	priv->mode = target_mode;
	return TRUE;
}

static gboolean
dfu_device_add_targets (DfuDevice *device, GError **error)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	g_autoptr(GPtrArray) ifaces = NULL;

	/* add all DFU-capable targets */
	ifaces = g_usb_device_get_interfaces (priv->dev, error);
	if (ifaces == NULL)
		return FALSE;
	g_ptr_array_set_size (priv->targets, 0);
	for (guint i = 0; i < ifaces->len; i++) {
		GBytes *iface_data = NULL;
		DfuTarget *target;
		const gchar *quirk_str;
		g_autoptr(GError) error_local = NULL;

		GUsbInterface *iface = g_ptr_array_index (ifaces, i);

		/* some devices don't use the right class and subclass */
		if (!dfu_device_has_quirk (device, DFU_DEVICE_QUIRK_USE_ANY_INTERFACE)) {
			if (g_usb_interface_get_class (iface) != G_USB_DEVICE_CLASS_APPLICATION_SPECIFIC)
				continue;
			if (g_usb_interface_get_subclass (iface) != 0x01)
				continue;
		}
		/* parse any interface data */
		iface_data = g_usb_interface_get_extra (iface);
		if (g_bytes_get_size (iface_data) > 0) {
			if (!dfu_device_parse_iface_data (device, iface_data, &error_local)) {
				g_warning ("failed to parse interface data: %s",
					   error_local->message);
				continue;
			}
		} else {
			priv->attributes |= DFU_DEVICE_ATTRIBUTE_CAN_DOWNLOAD |
					    DFU_DEVICE_ATTRIBUTE_CAN_UPLOAD;
		}

		/* fix up the version */
		quirk_str = fu_quirks_lookup_by_usb_device (priv->system_quirks,
							    FU_QUIRKS_DFU_FORCE_VERSION,
							    priv->dev);
		if (quirk_str != NULL && strlen (quirk_str) == 4)
			priv->version = dfu_utils_buffer_parse_uint16 (quirk_str);
		if (priv->version == DFU_VERSION_DFU_1_0 ||
		    priv->version == DFU_VERSION_DFU_1_1) {
			g_debug ("basic DFU 1.1");
		} else if (priv->version == DFU_VERSION_ATMEL_AVR) {
			g_debug ("AVR-DFU support");
			priv->version = DFU_VERSION_ATMEL_AVR;
		} else if (priv->version == DFU_VERSION_DFUSE) {
			g_debug ("STM-DFU support");
		} else if (priv->version == 0x0101) {
			g_debug ("basic DFU 1.1 assumed");
			priv->version = DFU_VERSION_DFU_1_1;
		} else {
			g_warning ("DFU version is invalid: 0x%04x", priv->version);
		}

		/* fix up the transfer size */
		if (priv->transfer_size == 0xffff) {
			priv->transfer_size = 0x0400;
			g_debug ("DFU transfer size unspecified, guessing");
		}
		if (priv->transfer_size > 0x0000) {
			g_debug ("using DFU transfer size 0x%04x bytes", priv->transfer_size);
		} else {
			g_warning ("DFU transfer size invalid, using default");
			priv->transfer_size = 64;
		}

		/* create a target of the required type */
		switch (priv->version) {
		case DFU_VERSION_DFUSE:
			target = dfu_target_stm_new ();
			break;
		case DFU_VERSION_ATMEL_AVR:
			target = dfu_target_avr_new ();
			break;
		default:
			target = dfu_target_new ();
			break;
		}
		dfu_target_set_device (target, device);
		dfu_target_set_alt_idx (target, g_usb_interface_get_index (iface));
		dfu_target_set_alt_setting (target, g_usb_interface_get_alternate (iface));

		/* add target */
		priv->iface_number = g_usb_interface_get_number (iface);
		g_ptr_array_add (priv->targets, target);
		dfu_device_update_from_iface (device, iface);
	}

	/* the device has no DFU runtime, so cheat */
	if (priv->quirks & DFU_DEVICE_QUIRK_NO_DFU_RUNTIME) {
		const gchar *quirk_str;
		if (priv->targets->len == 0) {
			g_debug ("no DFU runtime, so faking device");
			priv->iface_number = 0xff;
			priv->runtime_vid = g_usb_device_get_vid (priv->dev);
			priv->runtime_pid = g_usb_device_get_pid (priv->dev);
			priv->runtime_release = g_usb_device_get_release (priv->dev);
			priv->attributes = DFU_DEVICE_ATTRIBUTE_CAN_DOWNLOAD |
					   DFU_DEVICE_ATTRIBUTE_CAN_UPLOAD;
		}

		/* inverse, but it's the best we can do */
		quirk_str = fu_quirks_lookup_by_usb_device (priv->system_quirks,
							    FU_QUIRKS_DFU_ALTERNATE_VIDPID,
							    priv->dev);
		if (quirk_str != NULL && strlen (quirk_str) == 8) {
			priv->runtime_vid = dfu_utils_buffer_parse_uint16 (quirk_str + 0);
			priv->runtime_pid = dfu_utils_buffer_parse_uint16 (quirk_str + 4);
			g_debug ("using VID_%04X&PID_%04X as the runtime",
				 priv->runtime_vid, priv->runtime_pid);
		}
		return TRUE;
	}

	/* no targets */
	if (priv->targets->len == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "no DFU interfaces");
		return FALSE;
	}

	/* the device upload is broken */
	if (priv->quirks & DFU_DEVICE_QUIRK_IGNORE_UPLOAD)
		priv->attributes &= ~DFU_DEVICE_ATTRIBUTE_CAN_UPLOAD;

	return TRUE;
}

/**
 * dfu_device_has_quirk: (skip)
 * @device: A #DfuDevice
 * @quirk: A #DfuDeviceQuirks
 *
 * Returns if a device has a specific quirk
 *
 * Return value: %TRUE if the device has this quirk
 **/
gboolean
dfu_device_has_quirk (DfuDevice *device, DfuDeviceQuirks quirk)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	g_return_val_if_fail (DFU_IS_DEVICE (device), 0x0);
	return (priv->quirks & quirk) > 0;
}

/**
 * dfu_device_can_upload:
 * @device: a #GUsbDevice
 *
 * Gets if the device can upload.
 *
 * Return value: %TRUE if the device can upload from device to host
 **/
gboolean
dfu_device_can_upload (DfuDevice *device)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	g_return_val_if_fail (DFU_IS_DEVICE (device), FALSE);
	return (priv->attributes & DFU_DEVICE_ATTRIBUTE_CAN_UPLOAD) > 0;
}

/**
 * dfu_device_can_download:
 * @device: a #GUsbDevice
 *
 * Gets if the device can download.
 *
 * Return value: %TRUE if the device can download from host to device
 **/
gboolean
dfu_device_can_download (DfuDevice *device)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	g_return_val_if_fail (DFU_IS_DEVICE (device), FALSE);
	return (priv->attributes & DFU_DEVICE_ATTRIBUTE_CAN_DOWNLOAD) > 0;
}

/**
 * dfu_device_is_open:
 * @device: a #GUsbDevice
 *
 * Gets if the device is currently open.
 *
 * Return value: %TRUE if the device is open
 **/
gboolean
dfu_device_is_open (DfuDevice *device)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	g_return_val_if_fail (DFU_IS_DEVICE (device), FALSE);
	return priv->dev_locker != NULL;
}

/**
 * dfu_device_set_timeout:
 * @device: a #DfuDevice
 * @timeout_ms: the timeout in ms
 *
 * Sets the USB timeout to use when contacting the USB device.
 **/
void
dfu_device_set_timeout (DfuDevice *device, guint timeout_ms)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	g_return_if_fail (DFU_IS_DEVICE (device));
	priv->timeout_ms = timeout_ms;
}

/**
 * dfu_device_get_mode:
 * @device: a #GUsbDevice
 *
 * Gets the device mode.
 *
 * Return value: enumerated mode, e.g. %DFU_MODE_RUNTIME
 **/
DfuMode
dfu_device_get_mode (DfuDevice *device)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	g_return_val_if_fail (DFU_IS_DEVICE (device), DFU_MODE_UNKNOWN);
	return priv->mode;
}

/**
 * dfu_device_get_timeout:
 * @device: a #GUsbDevice
 *
 * Gets the device timeout.
 *
 * Return value: enumerated timeout in ms
 **/
guint
dfu_device_get_timeout (DfuDevice *device)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	g_return_val_if_fail (DFU_IS_DEVICE (device), 0);
	return priv->timeout_ms;
}

/**
 * dfu_device_get_state:
 * @device: a #GUsbDevice
 *
 * Gets the device state.
 *
 * Return value: enumerated state, e.g. %DFU_STATE_DFU_UPLOAD_IDLE
 **/
DfuState
dfu_device_get_state (DfuDevice *device)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	g_return_val_if_fail (DFU_IS_DEVICE (device), 0);
	return priv->state;
}

/**
 * dfu_device_get_status:
 * @device: a #GUsbDevice
 *
 * Gets the device status.
 *
 * Return value: enumerated status, e.g. %DFU_STATUS_ERR_ADDRESS
 **/
DfuStatus
dfu_device_get_status (DfuDevice *device)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	g_return_val_if_fail (DFU_IS_DEVICE (device), 0);
	return priv->status;
}

/**
 * dfu_device_has_attribute: (skip)
 * @device: A #DfuDevice
 * @attribute: A #DfuDeviceAttributes, e.g. %DFU_DEVICE_ATTRIBUTE_CAN_DOWNLOAD
 *
 * Returns if an attribute set for the device.
 *
 * Return value: %TRUE if the attribute is set
 **/
gboolean
dfu_device_has_attribute (DfuDevice *device, DfuDeviceAttributes attribute)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	g_return_val_if_fail (DFU_IS_DEVICE (device), 0x0);
	return (priv->attributes & attribute) > 0;
}

/**
 * dfu_device_remove_attribute: (skip)
 * @device: A #DfuDevice
 * @attribute: A #DfuDeviceAttributes, e.g. %DFU_DEVICE_ATTRIBUTE_CAN_DOWNLOAD
 *
 * Removes an attribute from the device.
 **/
void
dfu_device_remove_attribute (DfuDevice *device, DfuDeviceAttributes attribute)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	g_return_if_fail (DFU_IS_DEVICE (device));
	priv->attributes &= ~attribute;
}

static void
dfu_device_set_quirks_from_string (DfuDevice *device, const gchar *str)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	g_auto(GStrv) split = g_strsplit (str, "|", -1);
	for (guint i = 0; split[i] != NULL; i++) {
		if (g_strcmp0 (split[i], "ignore-polltimeout") == 0) {
			priv->quirks |= DFU_DEVICE_QUIRK_IGNORE_POLLTIMEOUT;
			continue;
		}
		if (g_strcmp0 (split[i], "force-dfu-mode") == 0) {
			priv->quirks |= DFU_DEVICE_QUIRK_FORCE_DFU_MODE;
			continue;
		}
		if (g_strcmp0 (split[i], "use-protocol-zero") == 0) {
			priv->quirks |= DFU_DEVICE_QUIRK_USE_PROTOCOL_ZERO;
			continue;
		}
		if (g_strcmp0 (split[i], "no-pid-change") == 0) {
			priv->quirks |= DFU_DEVICE_QUIRK_NO_PID_CHANGE;
			continue;
		}
		if (g_strcmp0 (split[i], "no-get-status-upload") == 0) {
			priv->quirks |= DFU_DEVICE_QUIRK_NO_GET_STATUS_UPLOAD;
			continue;
		}
		if (g_strcmp0 (split[i], "no-dfu-runtime") == 0) {
			priv->quirks |= DFU_DEVICE_QUIRK_NO_DFU_RUNTIME;
			continue;
		}
		if (g_strcmp0 (split[i], "attach-upload-download") == 0) {
			priv->quirks |= DFU_DEVICE_QUIRK_ATTACH_UPLOAD_DOWNLOAD;
			continue;
		}
		if (g_strcmp0 (split[i], "ignore-runtime") == 0) {
			priv->quirks |= DFU_DEVICE_QUIRK_IGNORE_RUNTIME;
			continue;
		}
		if (g_strcmp0 (split[i], "action-required") == 0) {
			priv->quirks |= DFU_DEVICE_QUIRK_ACTION_REQUIRED;
			continue;
		}
		if (g_strcmp0 (split[i], "ignore-upload") == 0) {
			priv->quirks |= DFU_DEVICE_QUIRK_IGNORE_UPLOAD;
			continue;
		}
		if (g_strcmp0 (split[i], "attach-extra-reset") == 0) {
			priv->quirks |= DFU_DEVICE_QUIRK_ATTACH_EXTRA_RESET;
			continue;
		}
		if (g_strcmp0 (split[i], "use-any-interface") == 0) {
			priv->quirks |= DFU_DEVICE_QUIRK_USE_ANY_INTERFACE;
			continue;
		}
	}
}

static void
dfu_device_apply_quirks (DfuDevice *device)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	if (priv->system_quirks != NULL && priv->dev != NULL) {
		const gchar *quirk_str;
		quirk_str = fu_quirks_lookup_by_usb_device (priv->system_quirks,
							    FU_QUIRKS_DFU,
							    priv->dev);
		if (quirk_str != NULL)
			dfu_device_set_quirks_from_string (device, quirk_str);
	} else {
		g_warning ("no system quirk information");
	}
}
void
dfu_device_set_system_quirks (DfuDevice *device, FuQuirks *quirks)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	g_set_object (&priv->system_quirks, quirks);
}

FuQuirks *
dfu_device_get_system_quirks (DfuDevice *device)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	return priv->system_quirks;
}

/**
 * dfu_device_new:
 *
 * Creates a new DFU device object.
 *
 * Return value: a new #DfuDevice
 **/
DfuDevice *
dfu_device_new (void)
{
	DfuDevice *device;
	device = g_object_new (DFU_TYPE_DEVICE, NULL);
	return device;
}

/**
 * dfu_device_get_targets:
 * @device: a #DfuDevice
 *
 * Gets all the targets for this device.
 *
 * Return value: (transfer none) (element-type DfuTarget): #DfuTarget, or %NULL
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
 **/
DfuTarget *
dfu_device_get_target_by_alt_setting (DfuDevice *device,
				      guint8 alt_setting,
				      GError **error)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);

	g_return_val_if_fail (DFU_IS_DEVICE (device), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* find by ID */
	for (guint i = 0; i < priv->targets->len; i++) {
		DfuTarget *target = g_ptr_array_index (priv->targets, i);
		if (dfu_target_get_alt_setting (target) == alt_setting)
			return g_object_ref (target);
	}

	/* failed */
	g_set_error (error,
		     FWUPD_ERROR,
		     FWUPD_ERROR_NOT_FOUND,
		     "No target with alt-setting %i",
		     alt_setting);
	return NULL;
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
 **/
DfuTarget *
dfu_device_get_target_by_alt_name (DfuDevice *device,
				   const gchar *alt_name,
				   GError **error)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);

	g_return_val_if_fail (DFU_IS_DEVICE (device), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* find by ID */
	for (guint i = 0; i < priv->targets->len; i++) {
		DfuTarget *target = g_ptr_array_index (priv->targets, i);
		if (g_strcmp0 (dfu_target_get_alt_name (target, NULL), alt_name) == 0)
			return g_object_ref (target);
	}

	/* failed */
	g_set_error (error,
		     FWUPD_ERROR,
		     FWUPD_ERROR_NOT_FOUND,
		     "No target with alt-name %s",
		     alt_name);
	return NULL;
}

/**
 * dfu_device_get_platform_id:
 * @device: a #DfuDevice
 *
 * Gets the platform ID which normally corresponds to the port in some way.
 *
 * Return value: string or %NULL
 **/
const gchar *
dfu_device_get_platform_id (DfuDevice *device)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	g_return_val_if_fail (DFU_IS_DEVICE (device), NULL);
	return priv->platform_id;
}

/**
 * dfu_device_get_runtime_vid:
 * @device: a #DfuDevice
 *
 * Gets the runtime vendor ID.
 *
 * Return value: vendor ID, or 0xffff for unknown
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
 **/
guint16
dfu_device_get_runtime_pid (DfuDevice *device)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	g_return_val_if_fail (DFU_IS_DEVICE (device), 0xffff);
	return priv->runtime_pid;
}

/**
 * dfu_device_get_runtime_release:
 * @device: a #DfuDevice
 *
 * Gets the runtime release number in BCD format.
 *
 * Return value: release number, or 0xffff for unknown
 **/
guint16
dfu_device_get_runtime_release (DfuDevice *device)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	g_return_val_if_fail (DFU_IS_DEVICE (device), 0xffff);
	return priv->runtime_release;
}

/**
 * dfu_device_get_usb_dev: (skip)
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
dfu_device_get_usb_dev (DfuDevice *device)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	g_return_val_if_fail (DFU_IS_DEVICE (device), NULL);
	return priv->dev;
}

/**
 * dfu_device_get_display_name:
 * @device: a #DfuDevice
 *
 * Gets the display name to use for the device.
 *
 * Return value: string or %NULL for unset
 **/
const gchar *
dfu_device_get_display_name (DfuDevice *device)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	g_return_val_if_fail (DFU_IS_DEVICE (device), NULL);
	return priv->display_name;
}

/**
 * dfu_device_get_serial_number:
 * @device: a #DfuDevice
 *
 * Gets the serial number for the device.
 *
 * Return value: string or %NULL for unset
 **/
const gchar *
dfu_device_get_serial_number (DfuDevice *device)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	g_return_val_if_fail (DFU_IS_DEVICE (device), NULL);
	return priv->serial_number;
}

const gchar *
dfu_device_get_chip_id (DfuDevice *device)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	g_return_val_if_fail (DFU_IS_DEVICE (device), NULL);
	return priv->chip_id;
}

void
dfu_device_set_chip_id (DfuDevice *device, const gchar *chip_id)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	g_return_if_fail (DFU_IS_DEVICE (device));
	g_debug ("chip ID set to: %s", chip_id);
	priv->chip_id = g_strdup (chip_id);
}

static void
dfu_device_set_state (DfuDevice *device, DfuState state)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	if (priv->state == state)
		return;
	priv->state = state;
	g_signal_emit (device, signals[SIGNAL_STATE_CHANGED], 0, state);
}

static void
dfu_device_set_status (DfuDevice *device, DfuStatus status)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	if (priv->status == status)
		return;
	priv->status = status;
	g_signal_emit (device, signals[SIGNAL_STATUS_CHANGED], 0, status);
}

gboolean
dfu_device_ensure_interface (DfuDevice *device,
			     GCancellable *cancellable,
			     GError **error)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	g_autoptr(GError) error_local = NULL;

	/* already done */
	if (priv->claimed_interface)
		return TRUE;

	/* nothing set */
	if (priv->iface_number == 0xff)
		return TRUE;

	/* claim, without detaching kernel driver */
	if (!g_usb_device_claim_interface (priv->dev,
					   (gint) priv->iface_number,
					   G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
					   &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "cannot claim interface %i: %s",
			     priv->iface_number, error_local->message);
		return FALSE;
	}

	/* success */
	priv->claimed_interface = TRUE;
	return TRUE;
}

/**
 * dfu_device_refresh:
 * @device: a #DfuDevice
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Refreshes the cached properties on the DFU device.
 *
 * Return value: %TRUE for success
 **/
gboolean
dfu_device_refresh (DfuDevice *device, GCancellable *cancellable, GError **error)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	gsize actual_length = 0;
	guint8 buf[6];
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail (DFU_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* no backing USB device */
	if (priv->dev == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "failed to refresh: no GUsbDevice for %s",
			     priv->platform_id);
		return FALSE;
	}

	/* the device has no DFU runtime, so cheat */
	if (priv->quirks & DFU_DEVICE_QUIRK_NO_DFU_RUNTIME) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "not supported as no DFU runtime");
		return FALSE;
	}

	/* ensure interface is claimed */
	if (!dfu_device_ensure_interface (device, cancellable, error))
		return FALSE;

	if (!g_usb_device_control_transfer (priv->dev,
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
			     "cannot get device state: %s",
			     error_local->message);
		return FALSE;
	}
	if (actual_length != 6) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "cannot get device status, invalid size: %04x",
			     (guint) actual_length);
		return FALSE;
	}

	/* status or state changed */
	dfu_device_set_status (device, buf[0]);
	dfu_device_set_state (device, buf[4]);
	if (dfu_device_has_quirk (device, DFU_DEVICE_QUIRK_IGNORE_POLLTIMEOUT)) {
		priv->dnload_timeout = 5;
	} else {
		priv->dnload_timeout = buf[1] +
					(((guint32) buf[2]) << 8) +
					(((guint32) buf[3]) << 16);
	}
	g_debug ("refreshed status=%s and state=%s (dnload=%u)",
		 dfu_status_to_string (priv->status),
		 dfu_state_to_string (priv->state),
		 priv->dnload_timeout);
	return TRUE;
}

static guint8
_g_usb_device_get_interface_for_class (GUsbDevice *dev,
				       guint8 intf_class,
				       GError **error)
{
	g_autoptr(GPtrArray) intfs = NULL;
	intfs = g_usb_device_get_interfaces (dev, error);
	if (intfs == NULL)
		return 0xff;
	for (guint i = 0; i < intfs->len; i++) {
		GUsbInterface *intf = g_ptr_array_index (intfs, i);
		if (g_usb_interface_get_class (intf) == intf_class)
			return g_usb_interface_get_number (intf);
	}
	return 0xff;
}

/**
 * dfu_device_detach:
 * @device: a #DfuDevice
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Detaches the device putting it into DFU-mode.
 *
 * Return value: %TRUE for success
 **/
gboolean
dfu_device_detach (DfuDevice *device, GCancellable *cancellable, GError **error)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	const guint16 timeout_reset_ms = 1000;
	const gchar *quirk_str;
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail (DFU_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* already in DFU mode */
	switch (priv->state) {
	case DFU_STATE_APP_IDLE:
	case DFU_STATE_APP_DETACH:
		break;
	default:
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "Already in DFU mode");
		return FALSE;
	}

	/* no backing USB device */
	if (priv->dev == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "failed to detach: no GUsbDevice for %s",
			     priv->platform_id);
		return FALSE;
	}

	/* handle Jabra devices that need a magic HID packet */
	quirk_str = fu_quirks_lookup_by_usb_device (priv->system_quirks,
						    FU_QUIRKS_DFU_JABRA_DETACH,
						    priv->dev);
	if (quirk_str != NULL) {
		guint8 adr = 0x00;
		guint8 rep = 0x00;
		guint8 iface_hid;
		g_autofree guint8 *buf = g_malloc0 (33);
		g_autoptr(GError) error_jabra = NULL;

		/* parse string and create magic packet */
		if (strlen (quirk_str) != 4) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "unsupported jabra quirk format: '%s'",
				     quirk_str);
			return FALSE;
		}
		rep = dfu_utils_buffer_parse_uint8 (quirk_str + 0);
		adr = dfu_utils_buffer_parse_uint8 (quirk_str + 2);
		buf[0] = rep;
		buf[1] = adr;
		buf[2] = 0x00;
		buf[3] = 0x01;
		buf[4] = 0x85;
		buf[5] = 0x07;

		/* detach the HID interface from the kernel driver */
		iface_hid = _g_usb_device_get_interface_for_class (priv->dev,
								   G_USB_DEVICE_CLASS_HID,
								   &error_local);
		if (iface_hid == 0xff) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "cannot find HID interface: %s",
				     error_local->message);
			return FALSE;
		}
		g_debug ("claiming interface 0x%02x", iface_hid);
		if (!g_usb_device_claim_interface (priv->dev, (gint) iface_hid,
						   G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
						   &error_local)) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "cannot claim interface 0x%02x: %s",
				     iface_hid, error_local->message);
			return FALSE;
		}

		/* send magic to device */
		if (!g_usb_device_control_transfer (priv->dev,
						    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
						    G_USB_DEVICE_REQUEST_TYPE_CLASS,
						    G_USB_DEVICE_RECIPIENT_INTERFACE,
						    0x09,
						    0x0200 | rep,
						    0x0003,
						    buf, 33, NULL,
						    5000,
						    cancellable,
						    &error_jabra)) {
			g_debug ("whilst sending magic: %s, ignoring",
				 error_jabra->message);
		}

		/* wait for device to re-appear */
		dfu_device_set_action (device, FWUPD_STATUS_DEVICE_RESTART);
		if (!dfu_device_wait_for_replug (device, 5000, cancellable, error))
			return FALSE;

		/* wait 10 seconds for DFU mode to settle */
		g_debug ("waiting for Jabra device to settle...");
		dfu_device_set_action (device, FWUPD_STATUS_DEVICE_BUSY);
		g_usleep (10 * G_USEC_PER_SEC);
	}

	/* the device has no DFU runtime, so cheat */
	if (priv->quirks & DFU_DEVICE_QUIRK_NO_DFU_RUNTIME) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "not supported as no DFU runtime");
		return FALSE;
	}

	/* ensure interface is claimed */
	if (!dfu_device_ensure_interface (device, cancellable, error))
		return FALSE;

	/* inform UI there's going to be a detach:attach */
	dfu_device_set_action (device, FWUPD_STATUS_DEVICE_RESTART);

	if (!g_usb_device_control_transfer (priv->dev,
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_CLASS,
					    G_USB_DEVICE_RECIPIENT_INTERFACE,
					    DFU_REQUEST_DETACH,
					    timeout_reset_ms,
					    priv->iface_number,
					    NULL, 0, NULL,
					    priv->timeout_ms,
					    cancellable,
					    &error_local)) {
		/* refresh the error code */
		dfu_device_error_fixup (device, cancellable, &error_local);
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "cannot detach device: %s",
			     error_local->message);
		return FALSE;
	}

	/* do a host reset */
	if ((priv->attributes & DFU_DEVICE_ATTRIBUTE_WILL_DETACH) == 0) {
		g_debug ("doing device reset as host will not self-reset");
		if (!dfu_device_reset (device, error))
			return FALSE;
	}

	/* success */
	dfu_device_set_action (device, FWUPD_STATUS_IDLE);
	return TRUE;
}

/**
 * dfu_device_abort:
 * @device: a #DfuDevice
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Aborts any upload or download in progress.
 *
 * Return value: %TRUE for success
 **/
gboolean
dfu_device_abort (DfuDevice *device, GCancellable *cancellable, GError **error)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail (DFU_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* no backing USB device */
	if (priv->dev == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "failed to abort: no GUsbDevice for %s",
			     priv->platform_id);
		return FALSE;
	}

	/* the device has no DFU runtime, so cheat */
	if (priv->quirks & DFU_DEVICE_QUIRK_NO_DFU_RUNTIME) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "not supported as no DFU runtime");
		return FALSE;
	}

	/* ensure interface is claimed */
	if (!dfu_device_ensure_interface (device, cancellable, error))
		return FALSE;

	if (!g_usb_device_control_transfer (priv->dev,
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
		/* refresh the error code */
		dfu_device_error_fixup (device, cancellable, &error_local);
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "cannot abort device: %s",
			     error_local->message);
		return FALSE;
	}

	return TRUE;
}

/**
 * dfu_device_clear_status:
 * @device: a #DfuDevice
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Clears any error status on the DFU device.
 *
 * Return value: %TRUE for success
 **/
gboolean
dfu_device_clear_status (DfuDevice *device, GCancellable *cancellable, GError **error)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail (DFU_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* no backing USB device */
	if (priv->dev == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "failed to clear status: no GUsbDevice for %s",
			     priv->platform_id);
		return FALSE;
	}

	/* the device has no DFU runtime, so cheat */
	if (priv->quirks & DFU_DEVICE_QUIRK_NO_DFU_RUNTIME) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "not supported as no DFU runtime");
		return FALSE;
	}

	/* ensure interface is claimed */
	if (!dfu_device_ensure_interface (device, cancellable, error))
		return FALSE;

	if (!g_usb_device_control_transfer (priv->dev,
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
		/* refresh the error code */
		dfu_device_error_fixup (device, cancellable, &error_local);
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "cannot clear status on the device: %s",
			     error_local->message);
		return FALSE;
	}
	return TRUE;
}

/**
 * dfu_device_get_interface:
 * @device: a #DfuDevice
 *
 * Gets the interface number.
 **/
guint8
dfu_device_get_interface (DfuDevice *device)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	g_return_val_if_fail (DFU_IS_DEVICE (device), 0xff);
	return priv->iface_number;
}

/**
 * dfu_device_open_full:
 * @device: a #DfuDevice
 * @flags: #DfuDeviceOpenFlags, e.g. %DFU_DEVICE_OPEN_FLAG_NONE
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Opens a DFU-capable device.
 *
 * Return value: %TRUE for success
 **/
gboolean
dfu_device_open_full (DfuDevice *device, DfuDeviceOpenFlags flags,
		      GCancellable *cancellable, GError **error)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	GPtrArray *targets = dfu_device_get_targets (device);
	guint8 idx;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;

	g_return_val_if_fail (DFU_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* no backing USB device */
	if (priv->dev == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "failed to open: no GUsbDevice for %s",
			     priv->platform_id);
		return FALSE;
	}

	/* already open */
	if (priv->dev_locker != NULL)
		return TRUE;

	/* open */
	locker = fu_device_locker_new (priv->dev, &error_local);
	if (locker == NULL) {
		if (g_error_matches (error_local,
				     G_USB_DEVICE_ERROR,
				     G_USB_DEVICE_ERROR_PERMISSION_DENIED)) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_PERMISSION_DENIED,
				     "%s", error_local->message);
			return FALSE;
		}
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "cannot open device %s: %s",
			     g_usb_device_get_platform_id (priv->dev),
			     error_local->message);
		return FALSE;
	}

	/* get product name if it exists */
	idx = g_usb_device_get_product_index (priv->dev);
	if (idx != 0x00)
		priv->display_name = g_usb_device_get_string_descriptor (priv->dev, idx, NULL);

	/* get serial number if it exists */
	idx = g_usb_device_get_serial_number_index (priv->dev);
	if (idx != 0x00)
		priv->serial_number = g_usb_device_get_string_descriptor (priv->dev, idx, NULL);

	/* the device has no DFU runtime, so cheat */
	if (priv->quirks & DFU_DEVICE_QUIRK_NO_DFU_RUNTIME) {
		priv->state = DFU_STATE_APP_IDLE;
		priv->status = DFU_STATUS_OK;
		priv->mode = DFU_MODE_RUNTIME;
		flags |= DFU_DEVICE_OPEN_FLAG_NO_AUTO_REFRESH;
	}

	/* device locker is now valid */
	priv->dev_locker = g_steal_pointer (&locker);

	/* automatically abort any uploads or downloads */
	if ((flags & DFU_DEVICE_OPEN_FLAG_NO_AUTO_REFRESH) == 0) {
		if (!dfu_device_refresh (device, cancellable, error))
			return FALSE;
		switch (priv->state) {
		case DFU_STATE_DFU_UPLOAD_IDLE:
		case DFU_STATE_DFU_DNLOAD_IDLE:
		case DFU_STATE_DFU_DNLOAD_SYNC:
			g_debug ("aborting transfer %s", dfu_status_to_string (priv->status));
			if (!dfu_device_abort (device, cancellable, error))
				return FALSE;
			break;
		case DFU_STATE_DFU_ERROR:
			g_debug ("clearing error %s", dfu_status_to_string (priv->status));
			if (!dfu_device_clear_status (device, cancellable, error))
				return FALSE;
			break;
		default:
			break;
		}
	}

	/* set up target ready for use */
	for (guint j = 0; j < targets->len; j++) {
		DfuTarget *target = g_ptr_array_index (targets, j);
		if (!dfu_target_setup (target, cancellable, error))
			return FALSE;
	}

	/* success */
	priv->open_new_dev = TRUE;
	return TRUE;
}

/**
 * dfu_device_open:
 * @device: a #DfuDevice
 * @flags: #DfuDeviceOpenFlags, e.g. %DFU_DEVICE_OPEN_FLAG_NONE
 * @error: a #GError, or %NULL
 *
 * Opens a DFU-capable device.
 *
 * Return value: %TRUE for success
 **/
gboolean
dfu_device_open (DfuDevice *device, GError **error)
{
	return dfu_device_open_full (device, DFU_DEVICE_OPEN_FLAG_NONE, NULL, error);
}

/**
 * dfu_device_close:
 * @device: a #DfuDevice
 * @error: a #GError, or %NULL
 *
 * Closes a DFU device.
 *
 * Return value: %TRUE for success
 **/
gboolean
dfu_device_close (DfuDevice *device, GError **error)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);

	/* no backing USB device */
	if (priv->dev == NULL)
		return TRUE;

	/* just clear the locker */
	g_clear_object (&priv->dev_locker);
	priv->claimed_interface = FALSE;
	priv->open_new_dev = FALSE;
	return TRUE;
}

gboolean
dfu_device_set_new_usb_dev (DfuDevice *device, GUsbDevice *dev,
			    GCancellable *cancellable, GError **error)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);

	/* same */
	if (priv->dev == dev) {
		g_warning ("setting GUsbDevice with same dev?!");
		return TRUE;
	}

	/* device removed */
	if (dev == NULL) {
		g_debug ("invalidating backing GUsbDevice");
		g_clear_object (&priv->dev_locker);
		g_clear_object (&priv->dev);
		g_ptr_array_set_size (priv->targets, 0);
		priv->claimed_interface = FALSE;
		return TRUE;
	}

	/* close */
	if (priv->dev != NULL) {
		gboolean tmp = priv->open_new_dev;
		g_clear_object (&priv->dev_locker);
		priv->open_new_dev = tmp;
	}

	/* set the new USB device */
	g_set_object (&priv->dev, dev);

	/* should be the same */
	if (g_strcmp0 (priv->platform_id,
		       g_usb_device_get_platform_id (dev)) != 0) {
		if (priv->platform_id != NULL)
			g_warning ("platform ID changed when setting new GUsbDevice?!");
		g_free (priv->platform_id);
		priv->platform_id = g_strdup (g_usb_device_get_platform_id (dev));
	}

	/* re-get the quirks for this new device */
	priv->quirks = DFU_DEVICE_QUIRK_NONE;
	priv->attributes = DFU_DEVICE_ATTRIBUTE_NONE;
	dfu_device_apply_quirks (device);

	/* update all the targets */
	if (!dfu_device_add_targets (device, error)) {
		g_prefix_error (error, "%04x:%04x is not supported: ",
				g_usb_device_get_vid (dev),
				g_usb_device_get_pid (dev));
		return FALSE;
	}

	/* reclaim */
	if (priv->open_new_dev) {
		g_debug ("automatically reopening device");
		if (!dfu_device_open_full (device, DFU_DEVICE_OPEN_FLAG_NONE,
					   cancellable, error))
			return FALSE;
	}
	return TRUE;
}

typedef struct {
	DfuDevice	*device;
	GError		**error;
	GMainLoop	*loop;
	GUsbDevice	*dev;
	guint		 cnt;
	guint		 timeout;
} DfuDeviceReplugHelper;

static void
dfu_device_replug_helper_free (DfuDeviceReplugHelper *helper)
{
	if (helper->dev != NULL)
		g_object_unref (helper->dev);
	g_object_unref (helper->device);
	g_main_loop_unref (helper->loop);
	g_free (helper);
}

static gboolean
dfu_device_replug_helper_cb (gpointer user_data)
{
	DfuDeviceReplugHelper *helper = (DfuDeviceReplugHelper *) user_data;
	DfuDevicePrivate *priv = GET_PRIVATE (helper->device);

	/* did the backing GUsbDevice change */
	if (helper->dev != priv->dev) {
		g_debug ("device changed GUsbDevice %p->%p",
			 helper->dev, priv->dev);
		g_set_object (&helper->dev, priv->dev);

		/* success */
		if (helper->dev != NULL) {
			g_main_loop_quit (helper->loop);
			return FALSE;
		}
	}

	/* set a limit */
	if (helper->cnt++ * 100 > helper->timeout) {
		g_debug ("gave up waiting for device replug");
		if (helper->dev == NULL) {
			g_set_error_literal (helper->error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_NOT_SUPPORTED,
					     "target went away but did not come back");
		} else {
			g_set_error_literal (helper->error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_NOT_SUPPORTED,
					     "target did not disconnect");
		}
		g_main_loop_quit (helper->loop);
		return FALSE;
	}

	/* continue waiting */
	g_debug ("waiting for device replug for %ums -- state is %s",
		 helper->cnt * 100, dfu_state_to_string (priv->state));
	return TRUE;
}

/**
 * dfu_device_wait_for_replug:
 * @device: a #DfuDevice
 * @timeout: the maximum amount of time to wait
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Waits for a DFU device to disconnect and reconnect.
 * This does rely on a #DfuContext being set up before this is called.
 *
 * Return value: %TRUE for success
 **/
gboolean
dfu_device_wait_for_replug (DfuDevice *device, guint timeout,
			    GCancellable *cancellable, GError **error)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	DfuDeviceReplugHelper *helper;
	GError *error_tmp = NULL;
	const guint replug_poll = 100; /* ms */

	/* wait for replug */
	helper = g_new0 (DfuDeviceReplugHelper, 1);
	helper->loop = g_main_loop_new (NULL, FALSE);
	helper->device = g_object_ref (device);
	helper->dev = g_object_ref (priv->dev);
	helper->error = &error_tmp;
	helper->timeout = timeout;
	g_timeout_add_full (G_PRIORITY_DEFAULT, replug_poll,
			    dfu_device_replug_helper_cb, helper,
			    (GDestroyNotify) dfu_device_replug_helper_free);
	g_main_loop_run (helper->loop);
	if (error_tmp != NULL) {
		g_propagate_error (error, error_tmp);
		return FALSE;
	}

	/* success */
	dfu_device_set_action (device, FWUPD_STATUS_IDLE);
	return TRUE;
}

/**
 * dfu_device_reset:
 * @device: a #DfuDevice
 * @error: a #GError, or %NULL
 *
 * Resets the USB device.
 *
 * Return value: %TRUE for success
 **/
gboolean
dfu_device_reset (DfuDevice *device, GError **error)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GTimer) timer = g_timer_new ();

	g_return_val_if_fail (DFU_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* no backing USB device */
	if (priv->dev == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "failed to reset: no GUsbDevice for %s",
			     priv->platform_id);
		return FALSE;
	}

	if (!g_usb_device_reset (priv->dev, &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "cannot reset USB device: %s [%i]",
			     error_local->message,
			     error_local->code);
		return FALSE;
	}
	g_debug ("reset took %.2lfms", g_timer_elapsed (timer, NULL) * 1000);
	return TRUE;
}

/**
 * dfu_device_attach:
 * @device: a #DfuDevice
 * @error: a #GError, or %NULL
 *
 * Move device from DFU mode to runtime.
 *
 * Return value: %TRUE for success
 **/
gboolean
dfu_device_attach (DfuDevice *device, GError **error)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	g_autoptr(DfuTarget) target = NULL;

	g_return_val_if_fail (DFU_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* already in runtime mode */
	switch (priv->state) {
	case DFU_STATE_APP_IDLE:
	case DFU_STATE_APP_DETACH:
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "Already in application runtime mode");
		return FALSE;
	default:
		break;
	}

	/* inform UI there's going to be a re-attach */
	dfu_device_set_action (device, FWUPD_STATUS_DEVICE_RESTART);

	/* handle m-stack DFU bootloaders */
	if (!priv->done_upload_or_download &&
	    (priv->quirks & DFU_DEVICE_QUIRK_ATTACH_UPLOAD_DOWNLOAD) > 0) {
		g_autoptr(GBytes) chunk = NULL;
		g_autoptr(DfuTarget) target_zero = NULL;
		g_debug ("doing dummy upload to work around m-stack quirk");
		target_zero = dfu_device_get_target_by_alt_setting (device, 0, error);
		if (target_zero == NULL)
			return FALSE;
		chunk = dfu_target_upload_chunk (target_zero, 0, 0, NULL, error);
		if (chunk == NULL)
			return FALSE;
	}

	/* get default target */
	target = dfu_device_get_target_by_alt_setting (device, 0, error);
	if (target == NULL)
		return FALSE;

	/* normal DFU mode just needs a bus reset */
	if (!dfu_target_attach (target, NULL, error))
		return FALSE;

	/* some devices need yet another reset */
	if (dfu_device_has_quirk (device, DFU_DEVICE_QUIRK_ATTACH_EXTRA_RESET)) {
		if (!dfu_device_wait_for_replug (device,
						 DFU_DEVICE_REPLUG_TIMEOUT,
						 NULL,
						 error))
			return FALSE;
		if (!dfu_device_reset (device, error))
			return FALSE;
	}

	/* success */
	dfu_device_set_action (device, FWUPD_STATUS_IDLE);
	return TRUE;
}

static void
dfu_device_percentage_cb (DfuTarget *target, guint percentage, DfuDevice *device)
{
	/* FIXME: divide by number of targets? */
	g_signal_emit (device, signals[SIGNAL_PERCENTAGE_CHANGED], 0, percentage);
}

static void
dfu_device_action_cb (DfuTarget *target, FwupdStatus action, DfuDevice *device)
{
	dfu_device_set_action (device, action);
}

/**
 * dfu_device_upload:
 * @device: a #DfuDevice
 * @flags: flags to use, e.g. %DFU_TARGET_TRANSFER_FLAG_VERIFY
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Uploads firmware from the target to the host.
 *
 * Return value: (transfer full): the uploaded firmware, or %NULL for error
 **/
DfuFirmware *
dfu_device_upload (DfuDevice *device,
		   DfuTargetTransferFlags flags,
		   GCancellable *cancellable,
		   GError **error)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	g_autoptr(DfuFirmware) firmware = NULL;

	/* no backing USB device */
	if (priv->dev == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "failed to upload: no GUsbDevice for %s",
			     priv->platform_id);
		return NULL;
	}

	/* ensure interface is claimed */
	if (!dfu_device_ensure_interface (device, cancellable, error))
		return NULL;

	/* create ahead of time */
	firmware = dfu_firmware_new ();
	dfu_firmware_set_vid (firmware, priv->runtime_vid);
	dfu_firmware_set_pid (firmware, priv->runtime_pid);
	dfu_firmware_set_release (firmware, 0xffff);

	/* APP -> DFU */
	if (priv->mode == DFU_MODE_RUNTIME) {
		if ((flags & DFU_TARGET_TRANSFER_FLAG_DETACH) == 0) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "device is not in DFU mode");
			return NULL;
		}
		g_debug ("detaching");

		/* detach and USB reset */
		if (!dfu_device_detach (device, NULL, error))
			return NULL;
		if (!dfu_device_wait_for_replug (device,
						 DFU_DEVICE_REPLUG_TIMEOUT,
						 cancellable,
						 error))
			return NULL;
	}

	/* upload from each target */
	for (guint i = 0; i < priv->targets->len; i++) {
		DfuTarget *target;
		const gchar *alt_name;
		gulong id1;
		gulong id2;
		g_autoptr(DfuImage) image = NULL;

		/* upload to target and proxy signals */
		target = g_ptr_array_index (priv->targets, i);

		/* ignore some target types */
		alt_name = dfu_target_get_alt_name_for_display (target, NULL);
		if (g_strcmp0 (alt_name, "Option Bytes") == 0) {
			g_debug ("ignoring target %s", alt_name);
			continue;
		}

		id1 = g_signal_connect (target, "percentage-changed",
					G_CALLBACK (dfu_device_percentage_cb), device);
		id2 = g_signal_connect (target, "action-changed",
					G_CALLBACK (dfu_device_action_cb), device);
		image = dfu_target_upload (target,
					   DFU_TARGET_TRANSFER_FLAG_NONE,
					   cancellable,
					   error);
		g_signal_handler_disconnect (target, id1);
		g_signal_handler_disconnect (target, id2);
		if (image == NULL)
			return NULL;
		dfu_firmware_add_image (firmware, image);
	}

	/* do not do the dummy upload for quirked devices */
	priv->done_upload_or_download = TRUE;

	/* choose the most appropriate type */
	if (priv->targets->len > 1) {
		g_debug ("switching to DefuSe automatically");
		dfu_firmware_set_format (firmware, DFU_FIRMWARE_FORMAT_DFUSE);
	} else {
		dfu_firmware_set_format (firmware, DFU_FIRMWARE_FORMAT_DFU);
	}

	/* do host reset */
	if ((flags & DFU_TARGET_TRANSFER_FLAG_ATTACH) > 0 ||
	    (flags & DFU_TARGET_TRANSFER_FLAG_WAIT_RUNTIME) > 0) {
		if (!dfu_device_attach (device, error))
			return NULL;
	}

	/* boot to runtime */
	if (flags & DFU_TARGET_TRANSFER_FLAG_WAIT_RUNTIME) {
		g_debug ("booting to runtime");
		if (!dfu_device_wait_for_replug (device,
						 DFU_DEVICE_REPLUG_TIMEOUT,
						 cancellable,
						 error))
			return NULL;
	}

	/* success */
	dfu_device_set_action (device, FWUPD_STATUS_IDLE);
	return g_object_ref (firmware);
}

static gboolean
dfu_device_id_compatible (guint16 id_file, guint16 id_runtime, guint16 id_dev)
{
	/* file doesn't specify */
	if (id_file == 0xffff)
		return TRUE;

	/* runtime matches */
	if (id_runtime != 0xffff && id_file == id_runtime)
		return TRUE;

	/* bootloader matches */
	if (id_dev != 0xffff && id_file == id_dev)
		return TRUE;

	/* nothing */
	return FALSE;
}

/**
 * dfu_device_download:
 * @device: a #DfuDevice
 * @firmware: a #DfuFirmware
 * @flags: flags to use, e.g. %DFU_TARGET_TRANSFER_FLAG_VERIFY
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Downloads firmware from the host to the target, optionally verifying
 * the transfer.
 *
 * Return value: %TRUE for success
 **/
gboolean
dfu_device_download (DfuDevice *device,
		     DfuFirmware *firmware,
		     DfuTargetTransferFlags flags,
		     GCancellable *cancellable,
		     GError **error)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	GPtrArray *images;
	gboolean ret;

	/* no backing USB device */
	if (priv->dev == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "failed to download: no GUsbDevice for %s",
			     priv->platform_id);
		return FALSE;
	}

	/* ensure interface is claimed */
	if (!dfu_device_ensure_interface (device, cancellable, error))
		return FALSE;

	/* do we allow wildcard VID:PID matches */
	if ((flags & DFU_TARGET_TRANSFER_FLAG_WILDCARD_VID) == 0) {
		if (dfu_firmware_get_vid (firmware) == 0xffff) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "firmware vendor ID not specified");
			return FALSE;
		}
	}
	if ((flags & DFU_TARGET_TRANSFER_FLAG_WILDCARD_PID) == 0) {
		if (dfu_firmware_get_pid (firmware) == 0xffff) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "firmware product ID not specified");
			return FALSE;
		}
	}

	/* check vendor matches */
	if (priv->runtime_vid != 0xffff) {
		if (!dfu_device_id_compatible (dfu_firmware_get_vid (firmware),
					       priv->runtime_vid,
					       g_usb_device_get_vid (priv->dev))) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "vendor ID incorrect, expected 0x%04x "
				     "got 0x%04x and 0x%04x\n",
				     dfu_firmware_get_vid (firmware),
				     priv->runtime_vid,
				     g_usb_device_get_vid (priv->dev));
			return FALSE;
		}
	}

	/* check product matches */
	if (priv->runtime_pid != 0xffff) {
		if (!dfu_device_id_compatible (dfu_firmware_get_pid (firmware),
					       priv->runtime_pid,
					       g_usb_device_get_pid (priv->dev))) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "product ID incorrect, expected 0x%04x "
				     "got 0x%04x and 0x%04x",
				     dfu_firmware_get_pid (firmware),
				     priv->runtime_pid,
				     g_usb_device_get_pid (priv->dev));
			return FALSE;
		}
	}

	/* APP -> DFU */
	if (priv->mode == DFU_MODE_RUNTIME) {
		if ((flags & DFU_TARGET_TRANSFER_FLAG_DETACH) == 0) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "device is not in DFU mode");
			return FALSE;
		}

		/* detach and USB reset */
		g_debug ("detaching");
		if (!dfu_device_detach (device, NULL, error))
			return FALSE;
		if (!dfu_device_wait_for_replug (device,
						 DFU_DEVICE_REPLUG_TIMEOUT,
						 NULL,
						 error))
			return FALSE;
	}

	/* download each target */
	images = dfu_firmware_get_images (firmware);
	if (images->len == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "no images in firmware file");
		return FALSE;
	}
	for (guint i = 0; i < images->len; i++) {
		DfuCipherKind cipher_fw;
		DfuCipherKind cipher_target;
		DfuImage *image;
		DfuTargetTransferFlags flags_local = DFU_TARGET_TRANSFER_FLAG_NONE;
		const gchar *alt_name;
		gulong id1;
		gulong id2;
		g_autoptr(DfuTarget) target_tmp = NULL;
		g_autoptr(GError) error_local = NULL;

		image = g_ptr_array_index (images, i);
		target_tmp = dfu_device_get_target_by_alt_setting (device,
								   dfu_image_get_alt_setting (image),
								   error);
		if (target_tmp == NULL)
			return FALSE;

		/* we don't actually need to print this, but it makes sure the
		 * target is setup prior to doing the cipher checks */
		alt_name = dfu_target_get_alt_name (target_tmp, &error_local);
		if (alt_name == NULL) {
			if (g_error_matches (error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND)) {
				alt_name = "unknown";
			} else {
				g_propagate_error (error, g_steal_pointer (&error_local));
				return FALSE;
			}
		}
		g_debug ("downloading to target: %s", alt_name);

		/* check we're flashing a compatible firmware */
		cipher_target = dfu_target_get_cipher_kind (target_tmp);
		cipher_fw = dfu_firmware_get_cipher_kind (firmware);
		if ((flags & DFU_TARGET_TRANSFER_FLAG_ANY_CIPHER) == 0) {
			if (cipher_fw != DFU_CIPHER_KIND_NONE &&
			    cipher_target == DFU_CIPHER_KIND_NONE) {
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INVALID_FILE,
					     "Device is only accepting "
					     "unsigned firmware, not %s",
					     dfu_cipher_kind_to_string (cipher_fw));
				return FALSE;
			}
			if (cipher_fw == DFU_CIPHER_KIND_NONE &&
			    cipher_target != DFU_CIPHER_KIND_NONE) {
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INVALID_FILE,
					     "Device is only accepting "
					     "firmware with %s cipher kind",
					     dfu_cipher_kind_to_string (cipher_target));
				return FALSE;
			}
		}

		/* download onto target */
		if (flags & DFU_TARGET_TRANSFER_FLAG_VERIFY)
			flags_local = DFU_TARGET_TRANSFER_FLAG_VERIFY;
		if (dfu_firmware_get_format (firmware) == DFU_FIRMWARE_FORMAT_RAW)
			flags_local |= DFU_TARGET_TRANSFER_FLAG_ADDR_HEURISTIC;
		id1 = g_signal_connect (target_tmp, "percentage-changed",
					G_CALLBACK (dfu_device_percentage_cb), device);
		id2 = g_signal_connect (target_tmp, "action-changed",
					G_CALLBACK (dfu_device_action_cb), device);
		ret = dfu_target_download (target_tmp,
					   image,
					   flags_local,
					   cancellable,
					   error);
		g_signal_handler_disconnect (target_tmp, id1);
		g_signal_handler_disconnect (target_tmp, id2);
		if (!ret)
			return FALSE;
	}

	/* do not do the dummy upload for quirked devices */
	priv->done_upload_or_download = TRUE;

	/* attempt to switch back to runtime */
	if ((flags & DFU_TARGET_TRANSFER_FLAG_ATTACH) > 0 ||
	    (flags & DFU_TARGET_TRANSFER_FLAG_WAIT_RUNTIME) > 0) {
		if (!dfu_device_attach (device, error))
			return FALSE;
	}

	/* boot to runtime */
	if (flags & DFU_TARGET_TRANSFER_FLAG_WAIT_RUNTIME) {
		g_debug ("booting to runtime to set auto-boot");
		if (!dfu_device_wait_for_replug (device,
						 DFU_DEVICE_REPLUG_TIMEOUT,
						 cancellable,
						 error))
			return FALSE;
	}

	/* success */
	dfu_device_set_action (device, FWUPD_STATUS_IDLE);
	return TRUE;
}

void
dfu_device_error_fixup (DfuDevice *device,
			GCancellable *cancellable,
			GError **error)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);

	/* sad panda */
	if (error == NULL)
		return;

	/* not the right error to query */
	if (!g_error_matches (*error,
			      G_USB_DEVICE_ERROR,
			      G_USB_DEVICE_ERROR_NOT_SUPPORTED))
		return;

	/* get the status */
	if (!dfu_device_refresh (device, cancellable, NULL))
		return;

	/* not in an error state */
	if (priv->state != DFU_STATE_DFU_ERROR)
		return;

	/* prefix the error */
	switch (priv->status) {
	case DFU_STATUS_OK:
		/* ignore */
		break;
	case DFU_STATUS_ERR_VENDOR:
		g_prefix_error (error, "read protection is active: ");
		break;
	default:
		g_prefix_error (error, "[%s,%s]: ",
				dfu_state_to_string (priv->state),
				dfu_status_to_string (priv->status));
		break;
	}
}

/**
 * dfu_device_get_quirks_as_string: (skip)
 * @device: a #DfuDevice
 *
 * Gets a string describing the quirks set for a device.
 *
 * Return value: string, or %NULL for no quirks
 **/
gchar *
dfu_device_get_quirks_as_string (DfuDevice *device)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	GString *str;

	/* just append to a string */
	str = g_string_new ("");
	if (priv->quirks & DFU_DEVICE_QUIRK_IGNORE_POLLTIMEOUT)
		g_string_append_printf (str, "ignore-polltimeout|");
	if (priv->quirks & DFU_DEVICE_QUIRK_FORCE_DFU_MODE)
		g_string_append_printf (str, "force-dfu-mode|");
	if (priv->quirks & DFU_DEVICE_QUIRK_USE_PROTOCOL_ZERO)
		g_string_append_printf (str, "use-protocol-zero|");
	if (priv->quirks & DFU_DEVICE_QUIRK_NO_PID_CHANGE)
		g_string_append_printf (str, "no-pid-change|");
	if (priv->quirks & DFU_DEVICE_QUIRK_NO_GET_STATUS_UPLOAD)
		g_string_append_printf (str, "no-get-status-upload|");
	if (priv->quirks & DFU_DEVICE_QUIRK_NO_DFU_RUNTIME)
		g_string_append_printf (str, "no-dfu-runtime|");
	if (priv->quirks & DFU_DEVICE_QUIRK_ATTACH_UPLOAD_DOWNLOAD)
		g_string_append_printf (str, "attach-upload-download|");
	if (priv->quirks & DFU_DEVICE_QUIRK_IGNORE_RUNTIME)
		g_string_append_printf (str, "ignore-runtime|");
	if (priv->quirks & DFU_DEVICE_QUIRK_ACTION_REQUIRED)
		g_string_append_printf (str, "action-required|");
	if (priv->quirks & DFU_DEVICE_QUIRK_IGNORE_UPLOAD)
		g_string_append_printf (str, "ignore-upload|");
	if (priv->quirks & DFU_DEVICE_QUIRK_ATTACH_EXTRA_RESET)
		g_string_append_printf (str, "attach-extra-reset|");
	if (priv->quirks & DFU_DEVICE_QUIRK_USE_ANY_INTERFACE)
		g_string_append_printf (str, "use-any-interface|");

	/* a well behaved device */
	if (str->len == 0) {
		g_string_free (str, TRUE);
		return NULL;
	}

	/* remove trailing pipe */
	g_string_truncate (str, str->len - 1);
	return g_string_free (str, FALSE);
}

/**
 * dfu_device_get_attributes_as_string: (skip)
 * @device: a #DfuDevice
 *
 * Gets a string describing the attributes for a device.
 *
 * Return value: a string, possibly empty
 **/
gchar *
dfu_device_get_attributes_as_string (DfuDevice *device)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	GString *str;

	/* just append to a string */
	str = g_string_new ("");
	if (priv->attributes & DFU_DEVICE_ATTRIBUTE_CAN_DOWNLOAD)
		g_string_append_printf (str, "can-download|");
	if (priv->attributes & DFU_DEVICE_ATTRIBUTE_CAN_UPLOAD)
		g_string_append_printf (str, "can-upload|");
	if (priv->attributes & DFU_DEVICE_ATTRIBUTE_MANIFEST_TOL)
		g_string_append_printf (str, "manifest-tol|");
	if (priv->attributes & DFU_DEVICE_ATTRIBUTE_WILL_DETACH)
		g_string_append_printf (str, "will-detach|");
	if (priv->attributes & DFU_DEVICE_ATTRIBUTE_CAN_ACCELERATE)
		g_string_append_printf (str, "can-accelerate|");

	/* remove trailing pipe */
	g_string_truncate (str, str->len - 1);
	return g_string_free (str, FALSE);
}
