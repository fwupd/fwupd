/*
 * Copyright (C) 2015-2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
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

/**
 * FU_QUIRKS_DFU_FLAGS:
 * @key: the USB device ID, e.g. `USB\VID_0763&PID_2806`
 * @value: a string, separated using `|`, e.g. `ignore-polltimeout|no-pid-change`
 *
 * Assigns optional quirks to use for a DFU device which does not follow the
 * DFU 1.0 or 1.1 specification. The list of supported quirks is thus:
 *
 * * `none`:			No device quirks
 * * `action-required`:		User has to do something manually, e.g. press a button
 * * `attach-extra-reset`:	Device needs resetting twice for attach
 * * `attach-upload-download`:	An upload or download is required for attach
 * * `force-dfu-mode`:		Force DFU mode
 * * `ignore-polltimeout`:	Ignore the device download timeout
 * * `ignore-runtime`:		Device has broken DFU runtime support
 * * `ignore-upload`:		Uploading from the device is broken
 * * `no-dfu-runtime`:		No DFU runtime interface is provided
 * * `no-get-status-upload`:	Do not do GetStatus when uploading
 * * `no-pid-change`:		Accept the same VID:PID when changing modes
 * * `use-any-interface`:	Use any interface for DFU
 * * `use-atmel-avr`:		Device uses the ATMEL bootloader
 * * `use-protocol-zero`:	Fix up the protocol number
 * * `legacy-protocol`:		Use a legacy protocol version
 *
 * Default value: `none`
 *
 * Since: 1.0.1
 */
#define	FU_QUIRKS_DFU_FLAGS			"DfuFlags"

/**
 * FU_QUIRKS_DFU_FORCE_VERSION:
 * @key: the USB device ID, e.g. `USB\VID_0763&PID_2806`
 * @value: the uint16_t DFU version, encoded in base 16, e.g. `0110`
 *
 * Forces a specific DFU version for the hardware device. This is required
 * if the device does not set, or sets incorrectly, items in the DFU functional
 * descriptor.
 *
 * Since: 1.0.1
 */
#define	FU_QUIRKS_DFU_FORCE_VERSION		"DfuForceVersion"

/**
 * FU_QUIRKS_DFU_JABRA_DETACH:
 * @key: the USB device ID, e.g. `USB\VID_0763&PID_2806`
 * @value: the two uint8_t unlock values, encoded in base 16, e.g. `0201`
 *
 * Assigns the two magic bytes sent to the Jabra hardware when the device is
 * in runtime mode to make it switch into DFU mode.
 *
 * Since: 1.0.1
 */
#define	FU_QUIRKS_DFU_JABRA_DETACH		"DfuJabraDetach"

#include "config.h"

#include <string.h>

#include "dfu-common.h"
#include "dfu-device-private.h"
#include "dfu-target-avr.h"
#include "dfu-target-private.h"
#include "dfu-target-stm.h"

#include "fu-device-locker.h"
#include "fu-firmware-common.h"

#include "fwupd-error.h"

static void dfu_device_finalize			 (GObject *object);

typedef struct {
	DfuDeviceAttributes	 attributes;
	DfuDeviceQuirks		 quirks;
	DfuState		 state;
	DfuStatus		 status;
	GPtrArray		*targets;
	GUsbContext		*usb_context;
	gboolean		 done_upload_or_download;
	gboolean		 claimed_interface;
	gchar			*chip_id;
	gchar			*jabra_detach;
	guint16			 version;
	guint16			 force_version;
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
	SIGNAL_LAST
};

static guint signals [SIGNAL_LAST] = { 0 };

G_DEFINE_TYPE_WITH_PRIVATE (DfuDevice, dfu_device, FU_TYPE_USB_DEVICE)
#define GET_PRIVATE(o) (dfu_device_get_instance_private (o))

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
	DfuFuncDescriptor desc = { 0x0 };
	const guint8 *buf;
	gsize sz;

	/* parse the functional descriptor */
	buf = g_bytes_get_data (iface_data, &sz);
	if (sz == sizeof(DfuFuncDescriptor)) {
		memcpy (&desc, buf, sz);
	} else if (sz > sizeof(DfuFuncDescriptor)) {
		g_debug ("DFU interface with %" G_GSIZE_FORMAT " bytes vendor data",
			 sz - sizeof(DfuFuncDescriptor));
		memcpy (&desc, buf, sizeof(DfuFuncDescriptor));
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

static void
dfu_device_guess_state_from_iface (DfuDevice *device, GUsbInterface *iface)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);

	/* some devices use the wrong interface */
	if (dfu_device_has_quirk (device, DFU_DEVICE_QUIRK_FORCE_DFU_MODE)) {
		g_debug ("quirking device into DFU mode");
		priv->state = DFU_STATE_DFU_IDLE;
		return;
	}

	/* runtime */
	if (g_usb_interface_get_protocol (iface) == 0x01) {
		priv->state = DFU_STATE_APP_IDLE;
		return;
	}

	/* DFU */
	if (g_usb_interface_get_protocol (iface) == 0x02) {
		priv->state = DFU_STATE_DFU_IDLE;
		return;
	}
	g_warning ("unable to guess initial device state from interface %u",
		   g_usb_interface_get_protocol (iface));
}

static gboolean
dfu_device_add_targets (DfuDevice *device, GError **error)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (device));
	g_autoptr(GPtrArray) ifaces = NULL;

	/* add all DFU-capable targets */
	ifaces = g_usb_device_get_interfaces (usb_device, error);
	if (ifaces == NULL)
		return FALSE;
	g_ptr_array_set_size (priv->targets, 0);
	for (guint i = 0; i < ifaces->len; i++) {
		GBytes *iface_data = NULL;
		DfuTarget *target;
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
				g_warning ("failed to parse interface data for %04x:%04x: %s",
					   g_usb_device_get_vid (usb_device),
					   g_usb_device_get_pid (usb_device),
					   error_local->message);
				continue;
			}
		} else {
			priv->attributes |= DFU_DEVICE_ATTRIBUTE_CAN_DOWNLOAD |
					    DFU_DEVICE_ATTRIBUTE_CAN_UPLOAD;
		}

		/* fix up the version */
		if (priv->force_version > 0)
			priv->version = priv->force_version;
		if (priv->version == DFU_VERSION_DFU_1_0 ||
		    priv->version == DFU_VERSION_DFU_1_1) {
			g_debug ("DFU v1.1");
		} else if (priv->version == DFU_VERSION_ATMEL_AVR) {
			g_debug ("AVR-DFU support");
			priv->version = DFU_VERSION_ATMEL_AVR;
		} else if (priv->version == DFU_VERSION_DFUSE) {
			g_debug ("STM-DFU support");
		} else if (priv->version == 0x0101) {
			g_debug ("DFU v1.1 assumed");
			priv->version = DFU_VERSION_DFU_1_1;
		} else {
			g_warning ("DFU version 0x%04x invalid, v1.1 assumed", priv->version);
			priv->version = DFU_VERSION_DFU_1_1;
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
		dfu_device_guess_state_from_iface (device, iface);
	}

	/* save for reset */
	if (priv->state == DFU_STATE_APP_IDLE ||
	    (priv->quirks & DFU_DEVICE_QUIRK_NO_PID_CHANGE)) {
		priv->runtime_vid = g_usb_device_get_vid (usb_device);
		priv->runtime_pid = g_usb_device_get_pid (usb_device);
		priv->runtime_release = g_usb_device_get_release (usb_device);
	}

	/* the device has no DFU runtime, so cheat */
	if (priv->targets->len == 0 &&
	    priv->quirks & DFU_DEVICE_QUIRK_NO_DFU_RUNTIME) {
		g_debug ("no DFU runtime, so faking device");
		priv->state = DFU_STATE_APP_IDLE;
		priv->iface_number = 0xff;
		priv->runtime_vid = g_usb_device_get_vid (usb_device);
		priv->runtime_pid = g_usb_device_get_pid (usb_device);
		priv->runtime_release = g_usb_device_get_release (usb_device);
		priv->attributes = DFU_DEVICE_ATTRIBUTE_CAN_DOWNLOAD |
				   DFU_DEVICE_ATTRIBUTE_CAN_UPLOAD;
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
 * dfu_device_is_runtime:
 * @device: a #GUsbDevice
 *
 * Gets the device mode.
 *
 * Return value: %TRUE if the device is in a runtime state
 **/
gboolean
dfu_device_is_runtime (DfuDevice *device)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	g_return_val_if_fail (DFU_IS_DEVICE (device), FALSE);
	if (priv->state == DFU_STATE_APP_IDLE ||
	    priv->state == DFU_STATE_APP_DETACH)
		return TRUE;
	return FALSE;
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
	g_auto(GStrv) split = g_strsplit (str, ",", -1);
	priv->quirks = DFU_DEVICE_QUIRK_NONE;
	for (guint i = 0; split[i] != NULL; i++) {
		if (g_strcmp0 (split[i], "ignore-polltimeout") == 0) {
			priv->quirks |= DFU_DEVICE_QUIRK_IGNORE_POLLTIMEOUT;
			continue;
		}
		if (g_strcmp0 (split[i], "force-dfu-mode") == 0) {
			priv->quirks |= DFU_DEVICE_QUIRK_FORCE_DFU_MODE;
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
		if (g_strcmp0 (split[i], "legacy-protocol") == 0) {
			priv->quirks |= DFU_DEVICE_QUIRK_LEGACY_PROTOCOL;
			continue;
		}
	}
}

void
dfu_device_set_usb_context (DfuDevice *device, GUsbContext *quirks)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	g_set_object (&priv->usb_context, quirks);
}

GUsbContext *
dfu_device_get_usb_context (DfuDevice *device)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	return priv->usb_context;
}

/**
 * dfu_device_new:
 *
 * Creates a new DFU device object.
 *
 * Return value: a new #DfuDevice
 **/
DfuDevice *
dfu_device_new (GUsbDevice *usb_device)
{
	DfuDevice *device;
	device = g_object_new (DFU_TYPE_DEVICE,
			       "usb-device", usb_device,
			       NULL);
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
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (device));
	g_return_val_if_fail (DFU_IS_DEVICE (device), NULL);
	return g_usb_device_get_platform_id (usb_device);
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
 * dfu_device_get_vid:
 * @device: a #DfuDevice
 *
 * Gets the present vendor ID.
 *
 * Return value: vendor ID, or 0xffff for unknown
 **/
guint16
dfu_device_get_vid (DfuDevice *device)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (device));
	g_return_val_if_fail (DFU_IS_DEVICE (device), 0xffff);
	return g_usb_device_get_vid (usb_device);
}

/**
 * dfu_device_get_pid:
 * @device: a #DfuDevice
 *
 * Gets the present product ID.
 *
 * Return value: product ID, or 0xffff for unknown
 **/
guint16
dfu_device_get_pid (DfuDevice *device)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (device));
	g_return_val_if_fail (DFU_IS_DEVICE (device), 0xffff);
	return g_usb_device_get_pid (usb_device);
}

/**
 * dfu_device_get_release:
 * @device: a #DfuDevice
 *
 * Gets the present release number in BCD format.
 *
 * Return value: release number, or 0xffff for unknown
 **/
guint16
dfu_device_get_release (DfuDevice *device)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (device));
	g_return_val_if_fail (DFU_IS_DEVICE (device), 0xffff);
	return g_usb_device_get_release (usb_device);
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
dfu_device_ensure_interface (DfuDevice *device, GError **error)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (device));
	g_autoptr(GError) error_local = NULL;

	/* already done */
	if (priv->claimed_interface)
		return TRUE;

	/* nothing set */
	if (priv->iface_number == 0xff)
		return TRUE;

	/* claim, without detaching kernel driver */
	if (!g_usb_device_claim_interface (usb_device,
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
 * dfu_device_refresh_and_clear:
 * @device: a #DfuDevice
 * @error: a #GError, or %NULL
 *
 * Refreshes the cached properties on the DFU device. If there are any transers
 * in progress thay are cancelled, and if there are any pending errors they are
 * cancelled.
 *
 * Return value: %TRUE for success
 **/
gboolean
dfu_device_refresh_and_clear (DfuDevice *device, GError **error)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	if (!dfu_device_refresh (device, error))
		return FALSE;
	switch (priv->state) {
	case DFU_STATE_DFU_UPLOAD_IDLE:
	case DFU_STATE_DFU_DNLOAD_IDLE:
	case DFU_STATE_DFU_DNLOAD_SYNC:
		g_debug ("aborting transfer %s", dfu_status_to_string (priv->status));
		if (!dfu_device_abort (device, error))
			return FALSE;
		break;
	case DFU_STATE_DFU_ERROR:
		g_debug ("clearing error %s", dfu_status_to_string (priv->status));
		if (!dfu_device_clear_status (device, error))
			return FALSE;
		break;
	default:
		break;
	}
	return TRUE;
}

/**
 * dfu_device_refresh:
 * @device: a #DfuDevice
 * @error: a #GError, or %NULL
 *
 * Refreshes the cached properties on the DFU device.
 *
 * Return value: %TRUE for success
 **/
gboolean
dfu_device_refresh (DfuDevice *device, GError **error)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (device));
	gsize actual_length = 0;
	guint8 buf[6];
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail (DFU_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* no backing USB device */
	if (usb_device == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "failed to refresh: no GUsbDevice for %s",
			     dfu_device_get_platform_id (device));
		return FALSE;
	}

	/* the device has no DFU runtime, so cheat */
	if (priv->state == DFU_STATE_APP_IDLE &&
	    priv->quirks & DFU_DEVICE_QUIRK_NO_DFU_RUNTIME)
		return TRUE;

	/* ensure interface is claimed */
	if (!dfu_device_ensure_interface (device, error))
		return FALSE;

	if (!g_usb_device_control_transfer (usb_device,
					    G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
					    G_USB_DEVICE_REQUEST_TYPE_CLASS,
					    G_USB_DEVICE_RECIPIENT_INTERFACE,
					    DFU_REQUEST_GETSTATUS,
					    0,
					    priv->iface_number,
					    buf, sizeof(buf), &actual_length,
					    priv->timeout_ms,
					    NULL, /* cancellable */
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

	/* some devices use the wrong state value */
	if (dfu_device_has_quirk (device, DFU_DEVICE_QUIRK_FORCE_DFU_MODE) &&
	    dfu_device_get_state (device) != DFU_STATE_DFU_IDLE) {
		g_debug ("quirking device into DFU mode");
		dfu_device_set_state (device, DFU_STATE_DFU_IDLE);
	} else {
		dfu_device_set_state (device, buf[4]);
	}

	/* status or state changed */
	dfu_device_set_status (device, buf[0]);
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
 * @error: a #GError, or %NULL
 *
 * Detaches the device putting it into DFU-mode.
 *
 * Return value: %TRUE for success
 **/
gboolean
dfu_device_detach (DfuDevice *device, GError **error)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (device));
	const guint16 timeout_reset_ms = 1000;
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail (DFU_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* already in DFU mode */
	if (!dfu_device_is_runtime (device)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "Already in DFU mode; state is %s",
			     dfu_state_to_string (priv->state));
		return FALSE;
	}

	/* no backing USB device */
	if (usb_device == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "failed to detach: no GUsbDevice for %s",
			     dfu_device_get_platform_id (device));
		return FALSE;
	}

	/* handle Jabra devices that need a magic HID packet */
	if (priv->jabra_detach != NULL) {
		guint8 adr = 0x00;
		guint8 rep = 0x00;
		guint8 iface_hid;
		g_autofree guint8 *buf = g_malloc0 (33);
		g_autoptr(GError) error_jabra = NULL;

		/* parse string and create magic packet */
		rep = fu_firmware_strparse_uint8 (priv->jabra_detach + 0);
		adr = fu_firmware_strparse_uint8 (priv->jabra_detach + 2);
		buf[0] = rep;
		buf[1] = adr;
		buf[2] = 0x00;
		buf[3] = 0x01;
		buf[4] = 0x85;
		buf[5] = 0x07;

		/* detach the HID interface from the kernel driver */
		iface_hid = _g_usb_device_get_interface_for_class (usb_device,
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
		if (!g_usb_device_claim_interface (usb_device, (gint) iface_hid,
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
		if (!g_usb_device_control_transfer (usb_device,
						    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
						    G_USB_DEVICE_REQUEST_TYPE_CLASS,
						    G_USB_DEVICE_RECIPIENT_INTERFACE,
						    0x09,
						    0x0200 | rep,
						    0x0003,
						    buf, 33, NULL,
						    FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE,
						    NULL, /* cancellable */
						    &error_jabra)) {
			g_debug ("whilst sending magic: %s, ignoring",
				 error_jabra->message);
		}

		/* wait for device to re-appear */
		fu_device_set_status (FU_DEVICE (device), FWUPD_STATUS_DEVICE_RESTART);
		if (!dfu_device_wait_for_replug (device, FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE, error))
			return FALSE;

		/* wait 10 seconds for DFU mode to settle */
		g_debug ("waiting for Jabra device to settle...");
		fu_device_set_status (FU_DEVICE (device), FWUPD_STATUS_DEVICE_BUSY);
		g_usleep (10 * G_USEC_PER_SEC);

		/* hacky workaround until Jabra has a plugin */
		usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (device));
	}

	/* the device has no DFU runtime, so cheat */
	if (priv->state == DFU_STATE_APP_IDLE &&
	    priv->quirks & DFU_DEVICE_QUIRK_NO_DFU_RUNTIME)
		return TRUE;

	/* ensure interface is claimed */
	if (!dfu_device_ensure_interface (device, error))
		return FALSE;

	/* inform UI there's going to be a detach:attach */
	fu_device_set_status (FU_DEVICE (device), FWUPD_STATUS_DEVICE_RESTART);

	if (!g_usb_device_control_transfer (usb_device,
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_CLASS,
					    G_USB_DEVICE_RECIPIENT_INTERFACE,
					    DFU_REQUEST_DETACH,
					    timeout_reset_ms,
					    priv->iface_number,
					    NULL, 0, NULL,
					    priv->timeout_ms,
					    NULL, /* cancellable */
					    &error_local)) {
		/* some devices just reboot and stall the endpoint :/ */
		if (g_error_matches (error_local,
				     G_USB_DEVICE_ERROR,
				     G_USB_DEVICE_ERROR_NOT_SUPPORTED) ||
		    g_error_matches (error_local,
				     G_USB_DEVICE_ERROR,
				     G_USB_DEVICE_ERROR_FAILED)) {
			g_debug ("ignoring while detaching: %s", error_local->message);
		} else {
			/* refresh the error code */
			dfu_device_error_fixup (device, &error_local);
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "cannot detach device: %s",
				     error_local->message);
			return FALSE;
		}
	}

	/* do a host reset */
	if ((priv->attributes & DFU_DEVICE_ATTRIBUTE_WILL_DETACH) == 0) {
		g_debug ("doing device reset as host will not self-reset");
		if (!dfu_device_reset (device, error))
			return FALSE;
	}

	/* success */
	priv->force_version = 0x0;
	fu_device_set_status (FU_DEVICE (device), FWUPD_STATUS_IDLE);
	return TRUE;
}

/**
 * dfu_device_abort:
 * @device: a #DfuDevice
 * @error: a #GError, or %NULL
 *
 * Aborts any upload or download in progress.
 *
 * Return value: %TRUE for success
 **/
gboolean
dfu_device_abort (DfuDevice *device, GError **error)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (device));
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail (DFU_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* no backing USB device */
	if (usb_device == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "failed to abort: no GUsbDevice for %s",
			     dfu_device_get_platform_id (device));
		return FALSE;
	}

	/* the device has no DFU runtime, so cheat */
	if (priv->state == DFU_STATE_APP_IDLE &&
	    priv->quirks & DFU_DEVICE_QUIRK_NO_DFU_RUNTIME) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "not supported as no DFU runtime");
		return FALSE;
	}

	/* ensure interface is claimed */
	if (!dfu_device_ensure_interface (device, error))
		return FALSE;

	if (!g_usb_device_control_transfer (usb_device,
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_CLASS,
					    G_USB_DEVICE_RECIPIENT_INTERFACE,
					    DFU_REQUEST_ABORT,
					    0,
					    priv->iface_number,
					    NULL, 0, NULL,
					    priv->timeout_ms,
					    NULL, /* cancellable */
					    &error_local)) {
		/* refresh the error code */
		dfu_device_error_fixup (device, &error_local);
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
 * @error: a #GError, or %NULL
 *
 * Clears any error status on the DFU device.
 *
 * Return value: %TRUE for success
 **/
gboolean
dfu_device_clear_status (DfuDevice *device, GError **error)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (device));
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail (DFU_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* no backing USB device */
	if (usb_device == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "failed to clear status: no GUsbDevice for %s",
			     dfu_device_get_platform_id (device));
		return FALSE;
	}

	/* the device has no DFU runtime, so cheat */
	if (priv->state == DFU_STATE_APP_IDLE &&
	    priv->quirks & DFU_DEVICE_QUIRK_NO_DFU_RUNTIME) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "not supported as no DFU runtime");
		return FALSE;
	}

	/* ensure interface is claimed */
	if (!dfu_device_ensure_interface (device, error))
		return FALSE;

	if (!g_usb_device_control_transfer (usb_device,
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_CLASS,
					    G_USB_DEVICE_RECIPIENT_INTERFACE,
					    DFU_REQUEST_CLRSTATUS,
					    0,
					    priv->iface_number,
					    NULL, 0, NULL,
					    priv->timeout_ms,
					    NULL, /* cancellable */
					    &error_local)) {
		/* refresh the error code */
		dfu_device_error_fixup (device, &error_local);
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
 * dfu_device_open:
 * @device: a #DfuDevice
 * @error: a #GError, or %NULL
 *
 * Opens a DFU-capable device.
 *
 * Return value: %TRUE for success
 **/
static gboolean
dfu_device_open (FuUsbDevice *device, GError **error)
{
	DfuDevice *self = DFU_DEVICE (device);
	DfuDevicePrivate *priv = GET_PRIVATE (self);
	GPtrArray *targets = dfu_device_get_targets (self);

	g_return_val_if_fail (DFU_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* the device has no DFU runtime, so cheat */
	if (priv->state == DFU_STATE_APP_IDLE &&
	    priv->quirks & DFU_DEVICE_QUIRK_NO_DFU_RUNTIME) {
		priv->state = DFU_STATE_APP_IDLE;
		priv->status = DFU_STATUS_OK;
	}

	/* set up target ready for use */
	for (guint j = 0; j < targets->len; j++) {
		DfuTarget *target = g_ptr_array_index (targets, j);
		if (!dfu_target_setup (target, error))
			return FALSE;
	}

	/* success */
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
 **/
static gboolean
dfu_device_close (FuUsbDevice *device, GError **error)
{
	DfuDevice *self = DFU_DEVICE (device);
	DfuDevicePrivate *priv = GET_PRIVATE (self);
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (device));

	/* release interface */
	if (priv->claimed_interface) {
		g_usb_device_release_interface (usb_device,
						(gint) priv->iface_number,
						0, NULL);
		priv->claimed_interface = FALSE;
	}

	return TRUE;
}

static gboolean
dfu_device_probe (FuUsbDevice *device, GError **error)
{
	DfuDevice *self = DFU_DEVICE (device);
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (device));

	/* add all the targets */
	if (!dfu_device_add_targets (self, error)) {
		g_prefix_error (error, "%04x:%04x is not supported: ",
				g_usb_device_get_vid (usb_device),
				g_usb_device_get_pid (usb_device));
		return FALSE;
	}

	/* check capabilities */
	if (dfu_device_can_download (self)) {
		fu_device_add_flag (FU_DEVICE (device), FWUPD_DEVICE_FLAG_UPDATABLE);
		fu_device_set_remove_delay (FU_DEVICE (device),
					    FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	}

	/* needs a manual action */
	if (dfu_device_has_quirk (self, DFU_DEVICE_QUIRK_ACTION_REQUIRED)) {
		fu_device_add_flag (FU_DEVICE (device),
				    FWUPD_DEVICE_FLAG_NEEDS_BOOTLOADER);
	}

	/* success */
	return TRUE;
}

/**
 * dfu_device_wait_for_replug:
 * @device: a #DfuDevice
 * @timeout: the maximum amount of time to wait
 * @error: a #GError, or %NULL
 *
 * Waits for a DFU device to disconnect and reconnect.
 * This does rely on a #GUsbContext being set up before this is called.
 *
 * Return value: %TRUE for success
 **/
gboolean
dfu_device_wait_for_replug (DfuDevice *device, guint timeout, GError **error)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (device));
	g_autoptr(GUsbDevice) usb_device2  = NULL;

	/* close */
	fu_device_close (FU_DEVICE (device), NULL);

	/* watch the device disappear and re-appear */
	usb_device2 = g_usb_context_wait_for_replug (priv->usb_context,
						     usb_device,
						     FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE,
						     error);
	if (usb_device2 == NULL)
		return FALSE;

	/* re-open with new device set */
	fu_device_set_status (FU_DEVICE (device), FWUPD_STATUS_IDLE);
	fu_usb_device_set_dev (FU_USB_DEVICE (device), usb_device2);
	if (!fu_device_open (FU_DEVICE (device), error))
		return FALSE;
	if (!dfu_device_refresh_and_clear (device, error))
		return FALSE;

	/* success */
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
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (device));
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GTimer) timer = g_timer_new ();

	g_return_val_if_fail (DFU_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* no backing USB device */
	if (usb_device == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "failed to reset: no GUsbDevice for %s",
			     dfu_device_get_platform_id (device));
		return FALSE;
	}

	if (!g_usb_device_reset (usb_device, &error_local)) {
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
	if (dfu_device_is_runtime (device)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "Already in application runtime mode");
		return FALSE;
	}

	/* inform UI there's going to be a re-attach */
	fu_device_set_status (FU_DEVICE (device), FWUPD_STATUS_DEVICE_RESTART);

	/* handle m-stack DFU bootloaders */
	if (!priv->done_upload_or_download &&
	    (priv->quirks & DFU_DEVICE_QUIRK_ATTACH_UPLOAD_DOWNLOAD) > 0) {
		g_autoptr(GBytes) chunk = NULL;
		g_autoptr(DfuTarget) target_zero = NULL;
		g_debug ("doing dummy upload to work around m-stack quirk");
		target_zero = dfu_device_get_target_by_alt_setting (device, 0, error);
		if (target_zero == NULL)
			return FALSE;
		chunk = dfu_target_upload_chunk (target_zero, 0, 0, error);
		if (chunk == NULL)
			return FALSE;
	}

	/* get default target */
	target = dfu_device_get_target_by_alt_setting (device, 0, error);
	if (target == NULL)
		return FALSE;

	/* normal DFU mode just needs a bus reset */
	if (!dfu_target_attach (target, error))
		return FALSE;

	/* some devices need yet another reset */
	if (dfu_device_has_quirk (device, DFU_DEVICE_QUIRK_ATTACH_EXTRA_RESET)) {
		if (!dfu_device_wait_for_replug (device,
						 FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE,
						 error))
			return FALSE;
		if (!dfu_device_reset (device, error))
			return FALSE;
	}

	/* success */
	priv->force_version = 0x0;
	fu_device_set_status (FU_DEVICE (device), FWUPD_STATUS_IDLE);
	return TRUE;
}

static void
dfu_device_percentage_cb (DfuTarget *target, guint percentage, DfuDevice *device)
{
	fu_device_set_progress (FU_DEVICE (device), percentage);
}

static void
dfu_device_action_cb (DfuTarget *target, FwupdStatus action, DfuDevice *device)
{
	fu_device_set_status (FU_DEVICE (device), action);
}

/**
 * dfu_device_upload:
 * @device: a #DfuDevice
 * @flags: flags to use, e.g. %DFU_TARGET_TRANSFER_FLAG_VERIFY
 * @error: a #GError, or %NULL
 *
 * Uploads firmware from the target to the host.
 *
 * Return value: (transfer full): the uploaded firmware, or %NULL for error
 **/
DfuFirmware *
dfu_device_upload (DfuDevice *device,
		   DfuTargetTransferFlags flags,
		   GError **error)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (device));
	g_autoptr(DfuFirmware) firmware = NULL;

	/* no backing USB device */
	if (usb_device == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "failed to upload: no GUsbDevice for %s",
			     dfu_device_get_platform_id (device));
		return NULL;
	}

	/* ensure interface is claimed */
	if (!dfu_device_ensure_interface (device, error))
		return NULL;

	/* create ahead of time */
	firmware = dfu_firmware_new ();
	dfu_firmware_set_vid (firmware, priv->runtime_vid);
	dfu_firmware_set_pid (firmware, priv->runtime_pid);
	dfu_firmware_set_release (firmware, 0xffff);

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

	/* success */
	fu_device_set_status (FU_DEVICE (device), FWUPD_STATUS_IDLE);
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
		     GError **error)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	GPtrArray *images;
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (device));
	gboolean ret;

	/* no backing USB device */
	if (usb_device == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "failed to download: no GUsbDevice for %s",
			     dfu_device_get_platform_id (device));
		return FALSE;
	}

	/* ensure interface is claimed */
	if (!dfu_device_ensure_interface (device, error))
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
					       dfu_device_get_vid (device))) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "vendor ID incorrect, expected 0x%04x "
				     "got 0x%04x and 0x%04x\n",
				     dfu_firmware_get_vid (firmware),
				     priv->runtime_vid,
				     dfu_device_get_vid (device));
			return FALSE;
		}
	}

	/* check product matches */
	if (priv->runtime_pid != 0xffff) {
		if (!dfu_device_id_compatible (dfu_firmware_get_pid (firmware),
					       priv->runtime_pid,
					       dfu_device_get_pid (device))) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "product ID incorrect, expected 0x%04x "
				     "got 0x%04x and 0x%04x",
				     dfu_firmware_get_pid (firmware),
				     priv->runtime_pid,
				     dfu_device_get_pid (device));
			return FALSE;
		}
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
					   error);
		g_signal_handler_disconnect (target_tmp, id1);
		g_signal_handler_disconnect (target_tmp, id2);
		if (!ret)
			return FALSE;
	}

	/* do not do the dummy upload for quirked devices */
	priv->done_upload_or_download = TRUE;

	/* success */
	fu_device_set_status (FU_DEVICE (device), FWUPD_STATUS_IDLE);
	return TRUE;
}

void
dfu_device_error_fixup (DfuDevice *device, GError **error)
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
	if (!dfu_device_refresh (device, NULL))
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
	if (priv->quirks & DFU_DEVICE_QUIRK_LEGACY_PROTOCOL)
		g_string_append_printf (str, "legacy-protocol|");

	/* a well behaved device */
	if (str->len == 0) {
		g_string_free (str, TRUE);
		return NULL;
	}

	/* remove trailing pipe */
	g_string_truncate (str, str->len - 1);
	return g_string_free (str, FALSE);
}

static gboolean
dfu_device_set_quirk_kv (FuDevice *device,
			 const gchar *key,
			 const gchar *value,
			 GError **error)
{
	DfuDevice *self = DFU_DEVICE (device);
	DfuDevicePrivate *priv = GET_PRIVATE (self);

	if (g_strcmp0 (key, FU_QUIRKS_DFU_FLAGS) == 0) {
		dfu_device_set_quirks_from_string (self, value);
		return TRUE;
	}
	if (g_strcmp0 (key, FU_QUIRKS_DFU_JABRA_DETACH) == 0) {
		if (value != NULL && strlen (value) == 4) {
			priv->jabra_detach = g_strdup (value);
			return TRUE;
		}
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "unsupported jabra quirk format");
		return FALSE;
	}
	if (g_strcmp0 (key, FU_QUIRKS_DFU_FORCE_VERSION) == 0) {
		if (value != NULL && strlen (value) == 4) {
			priv->force_version = fu_firmware_strparse_uint16 (value);
			return TRUE;
		}
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "invalid DFU version");
		return FALSE;
	}

	/* failed */
	g_set_error_literal (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_SUPPORTED,
			     "quirk key not supported");
	return FALSE;
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

static void
dfu_device_finalize (GObject *object)
{
	DfuDevice *device = DFU_DEVICE (object);
	DfuDevicePrivate *priv = GET_PRIVATE (device);

	if (priv->usb_context != NULL)
		g_object_unref (priv->usb_context);
	g_free (priv->chip_id);
	g_free (priv->jabra_detach);
	g_ptr_array_unref (priv->targets);

	G_OBJECT_CLASS (dfu_device_parent_class)->finalize (object);
}

static void
dfu_device_class_init (DfuDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	FuUsbDeviceClass *klass_usb_device = FU_USB_DEVICE_CLASS (klass);
	klass_device->set_quirk_kv = dfu_device_set_quirk_kv;
	klass_usb_device->open = dfu_device_open;
	klass_usb_device->close = dfu_device_close;
	klass_usb_device->probe = dfu_device_probe;

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
