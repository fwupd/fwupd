/*
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

/**
 * FuDfuDevice:
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
 * See also: [class@FuDfuTarget], [class@FuDfuseFirmware]
 */

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
#define FU_QUIRKS_DFU_FORCE_VERSION "DfuForceVersion"

#define DFU_DEVICE_DNLOAD_TIMEOUT_DEFAULT 5 /* ms */

#include "config.h"

#include <fwupdplugin.h>

#include <string.h>

#include "fu-dfu-common.h"
#include "fu-dfu-device.h"
#include "fu-dfu-target-avr.h"
#include "fu-dfu-target-private.h" /* waive-pre-commit */
#include "fu-dfu-target-stm.h"

static void
fu_dfu_device_finalize(GObject *object);

typedef struct {
	FuDfuDeviceAttrs attributes;
	FuDfuState state;
	FuDfuStatus status;
	GPtrArray *targets;
	gboolean done_upload_or_download;
	gboolean claimed_interface;
	gchar *chip_id;
	guint16 version;
	guint16 force_version;
	guint16 force_transfer_size;
	guint16 runtime_pid;
	guint16 runtime_vid;
	guint16 runtime_release;
	guint16 transfer_size;
	guint8 iface_number;
	guint dnload_timeout;
	guint timeout_ms;
} FuDfuDevicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuDfuDevice, fu_dfu_device, FU_TYPE_USB_DEVICE)
#define GET_PRIVATE(o) (fu_dfu_device_get_instance_private(o))

static void
fu_dfu_device_set_state(FuDfuDevice *self, FuDfuState state);

static void
fu_dfu_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuDfuDevice *self = FU_DFU_DEVICE(device);
	FuDfuDevicePrivate *priv = GET_PRIVATE(self);
	fu_common_string_append_kv(str, idt, "State", fu_dfu_state_to_string(priv->state));
	fu_common_string_append_kv(str, idt, "Status", fu_dfu_status_to_string(priv->status));
	fu_common_string_append_kb(str, idt, "DoneUploadOrDownload", priv->done_upload_or_download);
	fu_common_string_append_kb(str, idt, "ClaimedInterface", priv->claimed_interface);
	if (priv->chip_id != NULL)
		fu_common_string_append_kv(str, idt, "ChipId", priv->chip_id);
	fu_common_string_append_kx(str, idt, "Version", priv->version);
	fu_common_string_append_kx(str, idt, "ForceVersion", priv->force_version);
	if (priv->force_transfer_size != 0x0) {
		fu_common_string_append_kx(str,
					   idt,
					   "ForceTransferSize",
					   priv->force_transfer_size);
	}
	fu_common_string_append_kx(str, idt, "RuntimePid", priv->runtime_pid);
	fu_common_string_append_kx(str, idt, "RuntimeVid", priv->runtime_vid);
	fu_common_string_append_kx(str, idt, "RuntimeRelease", priv->runtime_release);
	fu_common_string_append_kx(str, idt, "TransferSize", priv->transfer_size);
	fu_common_string_append_kx(str, idt, "IfaceNumber", priv->iface_number);
	fu_common_string_append_kx(str, idt, "DnloadTimeout", priv->dnload_timeout);
	fu_common_string_append_kx(str, idt, "TimeoutMs", priv->timeout_ms);
	for (guint i = 0; i < priv->targets->len; i++) {
		FuDfuTarget *target = g_ptr_array_index(priv->targets, i);
		fu_dfu_target_to_string(target, idt + 1, str);
	}
}

/**
 * fu_dfu_device_get_transfer_size:
 * @device: a USB device
 *
 * Gets the transfer size in bytes.
 *
 * Returns: packet size, or 0 for unknown
 **/
guint16
fu_dfu_device_get_transfer_size(FuDfuDevice *self)
{
	FuDfuDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_DFU_DEVICE(self), 0xffff);
	return priv->transfer_size;
}

/**
 * fu_dfu_device_get_version:
 * @self: a #FuDfuDevice
 *
 * Gets the DFU specification version supported by the device.
 *
 * Returns: integer, or 0 for unknown, e.g. %FU_DFU_FIRMARE_VERSION_DFU_1_1
 **/
guint16
fu_dfu_device_get_version(FuDfuDevice *self)
{
	FuDfuDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_DFU_DEVICE(self), 0xffff);
	return priv->version;
}

/**
 * fu_dfu_device_get_download_timeout:
 * @self: a #FuDfuDevice
 *
 * Gets the download timeout in ms.
 *
 * Returns: delay, or 0 for unknown
 **/
guint
fu_dfu_device_get_download_timeout(FuDfuDevice *self)
{
	FuDfuDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_DFU_DEVICE(self), 0);
	return priv->dnload_timeout;
}

/**
 * fu_dfu_device_set_transfer_size:
 * @self: a #FuDfuDevice
 * @transfer_size: maximum packet size
 *
 * Sets the transfer size in bytes.
 **/
void
fu_dfu_device_set_transfer_size(FuDfuDevice *self, guint16 transfer_size)
{
	FuDfuDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_DFU_DEVICE(self));
	priv->transfer_size = transfer_size;
}

typedef struct __attribute__((packed)) {
	guint8 bLength;
	guint8 bDescriptorType;
	guint8 bmAttributes;
	guint16 wDetachTimeOut;
	guint16 wTransferSize;
	guint16 bcdDFUVersion;
} DfuFuncDescriptor;

static gboolean
fu_dfu_device_parse_iface_data(FuDfuDevice *self, GBytes *iface_data, GError **error)
{
	FuDfuDevicePrivate *priv = GET_PRIVATE(self);
	DfuFuncDescriptor desc = {0x0};
	const guint8 *buf;
	gsize sz;

	/* parse the functional descriptor */
	buf = g_bytes_get_data(iface_data, &sz);
	if (sz == sizeof(DfuFuncDescriptor)) {
		memcpy(&desc, buf, sz);
	} else if (sz > sizeof(DfuFuncDescriptor)) {
		g_debug("DFU interface with %" G_GSIZE_FORMAT " bytes vendor data",
			sz - sizeof(DfuFuncDescriptor));
		memcpy(&desc, buf, sizeof(DfuFuncDescriptor));
	} else if (sz == sizeof(DfuFuncDescriptor) - 2) {
		g_warning("truncated DFU interface data, no bcdDFUVersion");
		memcpy(&desc, buf, sz);
		desc.bcdDFUVersion = FU_DFU_FIRMARE_VERSION_DFU_1_1;
	} else {
		g_autoptr(GString) bufstr = g_string_new(NULL);
		for (gsize i = 0; i < sz; i++)
			g_string_append_printf(bufstr, "%02x ", buf[i]);
		if (bufstr->len > 0)
			g_string_truncate(bufstr, bufstr->len - 1);
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "interface found, but not the correct length for "
			    "functional data: %" G_GSIZE_FORMAT " bytes: %s",
			    sz,
			    bufstr->str);
		return FALSE;
	}

	/* get transfer size and version */
	priv->transfer_size = GUINT16_FROM_LE(desc.wTransferSize);
	priv->version = GUINT16_FROM_LE(desc.bcdDFUVersion);

	/* ST-specific */
	if (priv->version == FU_DFU_FIRMARE_VERSION_DFUSE &&
	    desc.bmAttributes & FU_DFU_DEVICE_ATTR_CAN_ACCELERATE)
		priv->transfer_size = 0x1000;

	/* get attributes about the DFU operation */
	priv->attributes = desc.bmAttributes;
	return TRUE;
}

static void
fu_dfu_device_guess_state_from_iface(FuDfuDevice *self, GUsbInterface *iface)
{
	/* some devices use the wrong interface */
	if (fu_device_has_private_flag(FU_DEVICE(self), FU_DFU_DEVICE_FLAG_FORCE_DFU_MODE)) {
		g_debug("quirking device into DFU mode");
		fu_dfu_device_set_state(self, FU_DFU_STATE_DFU_IDLE);
		return;
	}

	/* runtime */
	if (g_usb_interface_get_protocol(iface) == 0x01) {
		fu_dfu_device_set_state(self, FU_DFU_STATE_APP_IDLE);
		return;
	}

	/* DFU */
	if (g_usb_interface_get_protocol(iface) == 0x02) {
		fu_dfu_device_set_state(self, FU_DFU_STATE_DFU_IDLE);
		return;
	}
	g_warning("unable to guess initial device state from interface %u",
		  g_usb_interface_get_protocol(iface));
}

static gboolean
fu_dfu_device_add_targets(FuDfuDevice *self, GError **error)
{
	FuDfuDevicePrivate *priv = GET_PRIVATE(self);
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(self));
	g_autoptr(GPtrArray) ifaces = NULL;

	/* add all DFU-capable targets */
	ifaces = g_usb_device_get_interfaces(usb_device, error);
	if (ifaces == NULL)
		return FALSE;
	g_ptr_array_set_size(priv->targets, 0);
	for (guint i = 0; i < ifaces->len; i++) {
		GBytes *iface_data = NULL;
		FuDfuTarget *target;
		g_autoptr(GError) error_local = NULL;

		GUsbInterface *iface = g_ptr_array_index(ifaces, i);

		/* some devices don't use the right class and subclass */
		if (!fu_device_has_private_flag(FU_DEVICE(self),
						FU_DFU_DEVICE_FLAG_USE_ANY_INTERFACE)) {
			if (g_usb_interface_get_class(iface) !=
			    G_USB_DEVICE_CLASS_APPLICATION_SPECIFIC)
				continue;
			if (g_usb_interface_get_subclass(iface) != 0x01)
				continue;
		}
		/* parse any interface data */
		iface_data = g_usb_interface_get_extra(iface);
		if (g_bytes_get_size(iface_data) > 0) {
			if (!fu_dfu_device_parse_iface_data(self, iface_data, &error_local)) {
				g_warning("failed to parse interface data for %04x:%04x: %s",
					  g_usb_device_get_vid(usb_device),
					  g_usb_device_get_pid(usb_device),
					  error_local->message);
				continue;
			}
		} else {
			priv->attributes |=
			    FU_DFU_DEVICE_ATTR_CAN_DOWNLOAD | FU_DFU_DEVICE_ATTR_CAN_UPLOAD;
		}

		/* fix up the version */
		if (priv->force_version > 0)
			priv->version = priv->force_version;
		if (priv->version == FU_DFU_FIRMARE_VERSION_DFU_1_0 ||
		    priv->version == FU_DFU_FIRMARE_VERSION_DFU_1_1) {
			g_debug("DFU v1.1");
		} else if (priv->version == FU_DFU_FIRMARE_VERSION_ATMEL_AVR) {
			g_debug("AVR-DFU support");
			priv->version = FU_DFU_FIRMARE_VERSION_ATMEL_AVR;
		} else if (priv->version == FU_DFU_FIRMARE_VERSION_DFUSE) {
			g_debug("STM-DFU support");
		} else if (priv->version == 0x0101) {
			g_debug("DFU v1.1 assumed");
			priv->version = FU_DFU_FIRMARE_VERSION_DFU_1_1;
		} else {
			g_warning("DFU version 0x%04x invalid, v1.1 assumed", priv->version);
			priv->version = FU_DFU_FIRMARE_VERSION_DFU_1_1;
		}

		/* set expected protocol */
		if (priv->version == FU_DFU_FIRMARE_VERSION_DFUSE) {
			fu_device_add_protocol(FU_DEVICE(self), "com.st.dfuse");
		} else {
			fu_device_add_protocol(FU_DEVICE(self), "org.usb.dfu");
		}

		/* fix up the transfer size */
		if (priv->force_transfer_size != 0x0) {
			priv->transfer_size = priv->force_transfer_size;
			g_debug("forcing DFU transfer size 0x%04x bytes", priv->transfer_size);
		} else if (priv->transfer_size == 0xffff) {
			priv->transfer_size = 0x0400;
			g_debug("DFU transfer size unspecified, guessing");
		} else if (priv->transfer_size == 0x0) {
			g_warning("DFU transfer size invalid, using default");
			priv->transfer_size = 64;
		} else {
			g_debug("using DFU transfer size 0x%04x bytes", priv->transfer_size);
		}

		/* create a target of the required type */
		switch (priv->version) {
		case FU_DFU_FIRMARE_VERSION_DFUSE:
			target = fu_dfu_target_stm_new();
			break;
		case FU_DFU_FIRMARE_VERSION_ATMEL_AVR:
			target = fu_dfu_target_avr_new();
			break;
		default:
			target = fu_dfu_target_new();
			break;
		}
		fu_dfu_target_set_device(target, self);
		fu_dfu_target_set_alt_idx(target, g_usb_interface_get_index(iface));
		fu_dfu_target_set_alt_setting(target, g_usb_interface_get_alternate(iface));

		/* add target */
		priv->iface_number = g_usb_interface_get_number(iface);
		g_ptr_array_add(priv->targets, target);
		fu_dfu_device_guess_state_from_iface(self, iface);
	}

	/* save for reset */
	if (priv->state == FU_DFU_STATE_APP_IDLE ||
	    fu_device_has_private_flag(FU_DEVICE(self), FU_DFU_DEVICE_FLAG_NO_PID_CHANGE)) {
		priv->runtime_vid = g_usb_device_get_vid(usb_device);
		priv->runtime_pid = g_usb_device_get_pid(usb_device);
		priv->runtime_release = g_usb_device_get_release(usb_device);
	}

	/* the device has no DFU runtime, so cheat */
	if (priv->targets->len == 0 &&
	    fu_device_has_private_flag(FU_DEVICE(self), FU_DFU_DEVICE_FLAG_NO_DFU_RUNTIME)) {
		g_debug("no DFU runtime, so faking device");
		fu_dfu_device_set_state(self, FU_DFU_STATE_APP_IDLE);
		priv->iface_number = 0xff;
		priv->runtime_vid = g_usb_device_get_vid(usb_device);
		priv->runtime_pid = g_usb_device_get_pid(usb_device);
		priv->runtime_release = g_usb_device_get_release(usb_device);
		priv->attributes = FU_DFU_DEVICE_ATTR_CAN_DOWNLOAD | FU_DFU_DEVICE_ATTR_CAN_UPLOAD;
		return TRUE;
	}

	/* no targets */
	if (priv->targets->len == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no DFU interfaces");
		return FALSE;
	}

	/* the device upload is broken */
	if (fu_device_has_private_flag(FU_DEVICE(self), FU_DFU_DEVICE_FLAG_IGNORE_UPLOAD))
		priv->attributes &= ~FU_DFU_DEVICE_ATTR_CAN_UPLOAD;

	return TRUE;
}

/**
 * fu_dfu_device_can_upload:
 * @self: a #FuDfuDevice
 *
 * Gets if the device can upload.
 *
 * Returns: %TRUE if the device can upload from device to host
 **/
gboolean
fu_dfu_device_can_upload(FuDfuDevice *self)
{
	FuDfuDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_DFU_DEVICE(self), FALSE);
	return (priv->attributes & FU_DFU_DEVICE_ATTR_CAN_UPLOAD) > 0;
}

/**
 * fu_dfu_device_can_download:
 * @self: a #FuDfuDevice
 *
 * Gets if the device can download.
 *
 * Returns: %TRUE if the device can download from host to device
 **/
gboolean
fu_dfu_device_can_download(FuDfuDevice *self)
{
	FuDfuDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_DFU_DEVICE(self), FALSE);
	return (priv->attributes & FU_DFU_DEVICE_ATTR_CAN_DOWNLOAD) > 0;
}

/**
 * fu_dfu_device_set_timeout:
 * @self: a #FuDfuDevice
 * @timeout_ms: the timeout in ms
 *
 * Sets the USB timeout to use when contacting the USB device.
 **/
void
fu_dfu_device_set_timeout(FuDfuDevice *self, guint timeout_ms)
{
	FuDfuDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_DFU_DEVICE(self));
	priv->timeout_ms = timeout_ms;
}

/**
 * fu_dfu_device_get_timeout:
 * @device: a #FuDfuDevice
 *
 * Gets the device timeout.
 *
 * Returns: enumerated timeout in ms
 **/
guint
fu_dfu_device_get_timeout(FuDfuDevice *self)
{
	FuDfuDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_DFU_DEVICE(self), 0);
	return priv->timeout_ms;
}

/**
 * fu_dfu_device_get_state:
 * @device: a #FuDfuDevice
 *
 * Gets the device state.
 *
 * Returns: enumerated state, e.g. %FU_DFU_STATE_DFU_UPLOAD_IDLE
 **/
FuDfuState
fu_dfu_device_get_state(FuDfuDevice *self)
{
	FuDfuDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_DFU_DEVICE(self), 0);
	return priv->state;
}

/**
 * fu_dfu_device_get_status:
 * @device: a USB device
 *
 * Gets the device status.
 *
 * Returns: enumerated status, e.g. %FU_DFU_STATUS_ERR_ADDRESS
 **/
FuDfuStatus
fu_dfu_device_get_status(FuDfuDevice *self)
{
	FuDfuDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_DFU_DEVICE(self), 0);
	return priv->status;
}

/**
 * fu_dfu_device_has_attribute: (skip)
 * @self: a #FuDfuDevice
 * @attribute: a device attribute, e.g. %FU_DFU_DEVICE_ATTR_CAN_DOWNLOAD
 *
 * Returns if an attribute set for the device.
 *
 * Returns: %TRUE if the attribute is set
 **/
gboolean
fu_dfu_device_has_attribute(FuDfuDevice *self, FuDfuDeviceAttrs attribute)
{
	FuDfuDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_DFU_DEVICE(self), FALSE);
	return (priv->attributes & attribute) > 0;
}

/**
 * fu_dfu_device_remove_attribute: (skip)
 * @self: a #FuDfuDevice
 * @attribute: a device attribute, e.g. %FU_DFU_DEVICE_ATTR_CAN_DOWNLOAD
 *
 * Removes an attribute from the device.
 **/
void
fu_dfu_device_remove_attribute(FuDfuDevice *self, FuDfuDeviceAttrs attribute)
{
	FuDfuDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_DFU_DEVICE(self));
	priv->attributes &= ~attribute;
}

/**
 * fu_dfu_device_new:
 *
 * Creates a new DFU device object.
 *
 * Returns: a new #FuDfuDevice
 **/
FuDfuDevice *
fu_dfu_device_new(FuContext *ctx, GUsbDevice *usb_device)
{
	FuDfuDevice *self;
	self = g_object_new(FU_TYPE_DFU_DEVICE, "usb-device", usb_device, "context", ctx, NULL);
	return self;
}

/**
 * fu_dfu_device_get_targets:
 * @self: a #FuDfuDevice
 *
 * Gets all the targets for this device.
 *
 * Returns: (transfer none) (element-type FuDfuTarget): #FuDfuTarget, or %NULL
 **/
GPtrArray *
fu_dfu_device_get_targets(FuDfuDevice *self)
{
	FuDfuDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_DFU_DEVICE(self), NULL);
	return priv->targets;
}

/**
 * fu_dfu_device_get_target_by_alt_setting:
 * @self: a #FuDfuDevice
 * @alt_setting: the setting used to find
 * @error: (nullable): optional return location for an error
 *
 * Gets a target with a specific alternative setting.
 *
 * Returns: (transfer full): a #FuDfuTarget, or %NULL
 **/
FuDfuTarget *
fu_dfu_device_get_target_by_alt_setting(FuDfuDevice *self, guint8 alt_setting, GError **error)
{
	FuDfuDevicePrivate *priv = GET_PRIVATE(self);

	g_return_val_if_fail(FU_IS_DFU_DEVICE(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* find by ID */
	for (guint i = 0; i < priv->targets->len; i++) {
		FuDfuTarget *target = g_ptr_array_index(priv->targets, i);
		if (fu_dfu_target_get_alt_setting(target) == alt_setting)
			return g_object_ref(target);
	}

	/* failed */
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_FOUND,
		    "No target with alt-setting %i",
		    alt_setting);
	return NULL;
}

/**
 * fu_dfu_device_get_target_by_alt_name:
 * @self: a #FuDfuDevice
 * @alt_name: the name used to find
 * @error: (nullable): optional return location for an error
 *
 * Gets a target with a specific alternative name.
 *
 * Returns: (transfer full): a #FuDfuTarget, or %NULL
 **/
FuDfuTarget *
fu_dfu_device_get_target_by_alt_name(FuDfuDevice *self, const gchar *alt_name, GError **error)
{
	FuDfuDevicePrivate *priv = GET_PRIVATE(self);

	g_return_val_if_fail(FU_IS_DFU_DEVICE(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* find by ID */
	for (guint i = 0; i < priv->targets->len; i++) {
		FuDfuTarget *target = g_ptr_array_index(priv->targets, i);
		if (g_strcmp0(fu_dfu_target_get_alt_name(target, NULL), alt_name) == 0)
			return g_object_ref(target);
	}

	/* failed */
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_FOUND,
		    "No target with alt-name %s",
		    alt_name);
	return NULL;
}

/**
 * fu_dfu_device_get_platform_id:
 * @self: a #FuDfuDevice
 *
 * Gets the platform ID which normally corresponds to the port in some way.
 *
 * Returns: string or %NULL
 **/
const gchar *
fu_dfu_device_get_platform_id(FuDfuDevice *self)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(self));
	g_return_val_if_fail(FU_IS_DFU_DEVICE(self), NULL);
	return g_usb_device_get_platform_id(usb_device);
}

/**
 * fu_dfu_device_get_runtime_vid:
 * @self: a #FuDfuDevice
 *
 * Gets the runtime vendor ID.
 *
 * Returns: vendor ID, or 0xffff for unknown
 **/
guint16
fu_dfu_device_get_runtime_vid(FuDfuDevice *self)
{
	FuDfuDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_DFU_DEVICE(self), 0xffff);
	return priv->runtime_vid;
}

/**
 * fu_dfu_device_get_runtime_pid:
 * @self: a #FuDfuDevice
 *
 * Gets the runtime product ID.
 *
 * Returns: product ID, or 0xffff for unknown
 **/
guint16
fu_dfu_device_get_runtime_pid(FuDfuDevice *self)
{
	FuDfuDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_DFU_DEVICE(self), 0xffff);
	return priv->runtime_pid;
}

/**
 * fu_dfu_device_get_runtime_release:
 * @self: a #FuDfuDevice
 *
 * Gets the runtime release number in BCD format.
 *
 * Returns: release number, or 0xffff for unknown
 **/
guint16
fu_dfu_device_get_runtime_release(FuDfuDevice *self)
{
	FuDfuDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_DFU_DEVICE(self), 0xffff);
	return priv->runtime_release;
}

const gchar *
fu_dfu_device_get_chip_id(FuDfuDevice *self)
{
	FuDfuDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_DFU_DEVICE(self), NULL);
	return priv->chip_id;
}

void
fu_dfu_device_set_chip_id(FuDfuDevice *self, const gchar *chip_id)
{
	FuDfuDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_DFU_DEVICE(self));
	g_debug("chip ID set to: %s", chip_id);
	priv->chip_id = g_strdup(chip_id);
}

static void
fu_dfu_device_set_state(FuDfuDevice *self, FuDfuState state)
{
	FuDfuDevicePrivate *priv = GET_PRIVATE(self);
	if (priv->state == state)
		return;
	priv->state = state;

	/* set bootloader status */
	if (state == FU_DFU_STATE_APP_IDLE || state == FU_DFU_STATE_APP_DETACH) {
		fu_device_remove_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	} else {
		fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	}
}

static void
fu_dfu_device_set_status(FuDfuDevice *self, FuDfuStatus status)
{
	FuDfuDevicePrivate *priv = GET_PRIVATE(self);
	if (priv->status == status)
		return;
	priv->status = status;
}

gboolean
fu_dfu_device_ensure_interface(FuDfuDevice *self, GError **error)
{
	FuDfuDevicePrivate *priv = GET_PRIVATE(self);
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(self));
	g_autoptr(GError) error_local = NULL;

	/* already done */
	if (priv->claimed_interface)
		return TRUE;

	/* nothing set */
	if (priv->iface_number == 0xff)
		return TRUE;

	/* claim, without detaching kernel driver */
	if (!g_usb_device_claim_interface(usb_device,
					  (gint)priv->iface_number,
					  G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
					  &error_local)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "cannot claim interface %i: %s",
			    priv->iface_number,
			    error_local->message);
		return FALSE;
	}

	/* success */
	priv->claimed_interface = TRUE;
	return TRUE;
}

/**
 * fu_dfu_device_refresh_and_clear:
 * @self: a #FuDfuDevice
 * @error: (nullable): optional return location for an error
 *
 * Refreshes the cached properties on the DFU device. If there are any transers
 * in progress they are cancelled, and if there are any pending errors they are
 * cancelled.
 *
 * Returns: %TRUE for success
 **/
gboolean
fu_dfu_device_refresh_and_clear(FuDfuDevice *self, GError **error)
{
	FuDfuDevicePrivate *priv = GET_PRIVATE(self);
	if (!fu_dfu_device_refresh(self, error))
		return FALSE;
	switch (priv->state) {
	case FU_DFU_STATE_DFU_UPLOAD_IDLE:
	case FU_DFU_STATE_DFU_DNLOAD_IDLE:
	case FU_DFU_STATE_DFU_DNLOAD_SYNC:
		g_debug("aborting transfer %s", fu_dfu_status_to_string(priv->status));
		if (!fu_dfu_device_abort(self, error))
			return FALSE;
		break;
	case FU_DFU_STATE_DFU_ERROR:
		g_debug("clearing error %s", fu_dfu_status_to_string(priv->status));
		if (!fu_dfu_device_clear_status(self, error))
			return FALSE;
		break;
	default:
		break;
	}
	return TRUE;
}

/**
 * fu_dfu_device_refresh:
 * @self: a #FuDfuDevice
 * @error: (nullable): optional return location for an error
 *
 * Refreshes the cached properties on the DFU device.
 *
 * Returns: %TRUE for success
 **/
gboolean
fu_dfu_device_refresh(FuDfuDevice *self, GError **error)
{
	FuDfuDevicePrivate *priv = GET_PRIVATE(self);
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(self));
	gsize actual_length = 0;
	guint8 buf[6];
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail(FU_IS_DFU_DEVICE(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* no backing USB device */
	if (usb_device == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "failed to refresh: no GUsbDevice for %s",
			    fu_dfu_device_get_platform_id(self));
		return FALSE;
	}

	/* the device has no DFU runtime, so cheat */
	if (priv->state == FU_DFU_STATE_APP_IDLE &&
	    fu_device_has_private_flag(FU_DEVICE(self), FU_DFU_DEVICE_FLAG_NO_DFU_RUNTIME))
		return TRUE;

	/* ensure interface is claimed */
	if (!fu_dfu_device_ensure_interface(self, error))
		return FALSE;

	/* Device that cannot communicate via the USB after the
	 * Manifestation phase indicated this limitation to the
	 * host by clearing bmAttributes bit bitManifestationTolerant.
	 * so we assume the operation was successful */
	if (priv->state == FU_DFU_STATE_DFU_MANIFEST &&
	    !(priv->attributes & FU_DFU_DEVICE_ATTR_MANIFEST_TOL))
		return TRUE;

	if (!g_usb_device_control_transfer(usb_device,
					   G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
					   G_USB_DEVICE_REQUEST_TYPE_CLASS,
					   G_USB_DEVICE_RECIPIENT_INTERFACE,
					   FU_DFU_REQUEST_GETSTATUS,
					   0,
					   priv->iface_number,
					   buf,
					   sizeof(buf),
					   &actual_length,
					   priv->timeout_ms,
					   NULL, /* cancellable */
					   &error_local)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "cannot get device state: %s",
			    error_local->message);
		return FALSE;
	}
	if (actual_length != 6) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "cannot get device status, invalid size: %04x",
			    (guint)actual_length);
		return FALSE;
	}

	/* some devices use the wrong state value */
	if (fu_device_has_private_flag(FU_DEVICE(self), FU_DFU_DEVICE_FLAG_FORCE_DFU_MODE) &&
	    fu_dfu_device_get_state(self) != FU_DFU_STATE_DFU_IDLE) {
		g_debug("quirking device into DFU mode");
		fu_dfu_device_set_state(self, FU_DFU_STATE_DFU_IDLE);
	} else {
		fu_dfu_device_set_state(self, buf[4]);
	}

	/* status or state changed */
	fu_dfu_device_set_status(self, buf[0]);
	if (fu_device_has_private_flag(FU_DEVICE(self), FU_DFU_DEVICE_FLAG_IGNORE_POLLTIMEOUT)) {
		priv->dnload_timeout = DFU_DEVICE_DNLOAD_TIMEOUT_DEFAULT;
	} else {
		priv->dnload_timeout =
		    buf[1] + (((guint32)buf[2]) << 8) + (((guint32)buf[3]) << 16);
		if (priv->dnload_timeout == 0 &&
		    !fu_device_has_private_flag(FU_DEVICE(self),
						FU_DFU_DEVICE_FLAG_ALLOW_ZERO_POLLTIMEOUT)) {
			priv->dnload_timeout = DFU_DEVICE_DNLOAD_TIMEOUT_DEFAULT;
			g_debug("no dnload-timeout, using default of %ums", priv->dnload_timeout);
		}
	}
	g_debug("refreshed status=%s and state=%s (dnload=%u)",
		fu_dfu_status_to_string(priv->status),
		fu_dfu_state_to_string(priv->state),
		priv->dnload_timeout);
	return TRUE;
}

static gboolean
fu_dfu_device_request_detach(FuDfuDevice *self, FuProgress *progress, GError **error)
{
	FuDfuDevicePrivate *priv = GET_PRIVATE(self);
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(self));
	const guint16 timeout_reset_ms = 1000;
	g_autoptr(GError) error_local = NULL;

	if (!g_usb_device_control_transfer(usb_device,
					   G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					   G_USB_DEVICE_REQUEST_TYPE_CLASS,
					   G_USB_DEVICE_RECIPIENT_INTERFACE,
					   FU_DFU_REQUEST_DETACH,
					   timeout_reset_ms,
					   priv->iface_number,
					   NULL,
					   0,
					   NULL,
					   priv->timeout_ms,
					   NULL, /* cancellable */
					   &error_local)) {
		/* some devices just reboot and stall the endpoint :/ */
		if (g_error_matches(error_local,
				    G_USB_DEVICE_ERROR,
				    G_USB_DEVICE_ERROR_NOT_SUPPORTED) ||
		    g_error_matches(error_local, G_USB_DEVICE_ERROR, G_USB_DEVICE_ERROR_FAILED)) {
			g_debug("ignoring while detaching: %s", error_local->message);
		} else {
			/* refresh the error code */
			fu_dfu_device_error_fixup(self, &error_local);
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "cannot detach device: %s",
				    error_local->message);
			return FALSE;
		}
	}
	return TRUE;
}

static gboolean
fu_dfu_device_reload(FuDevice *device, GError **error)
{
	FuDfuDevice *self = FU_DFU_DEVICE(device);
	return fu_dfu_device_refresh_and_clear(self, error);
}

static gboolean
fu_dfu_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuDfuDevice *self = FU_DFU_DEVICE(device);
	FuDfuDevicePrivate *priv = GET_PRIVATE(self);
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(device));

	g_return_val_if_fail(FU_IS_DFU_DEVICE(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* already in DFU mode */
	if (!fu_dfu_device_refresh_and_clear(self, error))
		return FALSE;
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER))
		return TRUE;

	/* no backing USB device */
	if (usb_device == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "failed to detach: no GUsbDevice for %s",
			    fu_dfu_device_get_platform_id(self));
		return FALSE;
	}

	/* the device has no DFU runtime, so cheat */
	if (priv->state == FU_DFU_STATE_APP_IDLE &&
	    fu_device_has_private_flag(FU_DEVICE(self), FU_DFU_DEVICE_FLAG_NO_DFU_RUNTIME))
		return TRUE;

	/* ensure interface is claimed */
	if (!fu_dfu_device_ensure_interface(self, error))
		return FALSE;

	/* inform UI there's going to be a detach:attach */
	if (!fu_dfu_device_request_detach(self, progress, error))
		return FALSE;

	/* do a host reset */
	if ((priv->attributes & FU_DFU_DEVICE_ATTR_WILL_DETACH) == 0) {
		g_debug("doing device reset as host will not self-reset");
		if (!fu_dfu_device_reset(self, progress, error))
			return FALSE;
	}

	/* success */
	priv->force_version = 0x0;
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

/**
 * fu_dfu_device_abort:
 * @self: a #FuDfuDevice
 * @error: (nullable): optional return location for an error
 *
 * Aborts any upload or download in progress.
 *
 * Returns: %TRUE for success
 **/
gboolean
fu_dfu_device_abort(FuDfuDevice *self, GError **error)
{
	FuDfuDevicePrivate *priv = GET_PRIVATE(self);
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(self));
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail(FU_IS_DFU_DEVICE(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* no backing USB device */
	if (usb_device == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "failed to abort: no GUsbDevice for %s",
			    fu_dfu_device_get_platform_id(self));
		return FALSE;
	}

	/* the device has no DFU runtime, so cheat */
	if (priv->state == FU_DFU_STATE_APP_IDLE &&
	    fu_device_has_private_flag(FU_DEVICE(self), FU_DFU_DEVICE_FLAG_NO_DFU_RUNTIME)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "not supported as no DFU runtime");
		return FALSE;
	}

	/* ensure interface is claimed */
	if (!fu_dfu_device_ensure_interface(self, error))
		return FALSE;

	if (!g_usb_device_control_transfer(usb_device,
					   G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					   G_USB_DEVICE_REQUEST_TYPE_CLASS,
					   G_USB_DEVICE_RECIPIENT_INTERFACE,
					   FU_DFU_REQUEST_ABORT,
					   0,
					   priv->iface_number,
					   NULL,
					   0,
					   NULL,
					   priv->timeout_ms,
					   NULL, /* cancellable */
					   &error_local)) {
		/* refresh the error code */
		fu_dfu_device_error_fixup(self, &error_local);
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "cannot abort device: %s",
			    error_local->message);
		return FALSE;
	}

	return TRUE;
}

/**
 * fu_dfu_device_clear_status:
 * @self: a #FuDfuDevice
 * @error: (nullable): optional return location for an error
 *
 * Clears any error status on the DFU device.
 *
 * Returns: %TRUE for success
 **/
gboolean
fu_dfu_device_clear_status(FuDfuDevice *self, GError **error)
{
	FuDfuDevicePrivate *priv = GET_PRIVATE(self);
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(self));
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail(FU_IS_DFU_DEVICE(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* no backing USB device */
	if (usb_device == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "failed to clear status: no GUsbDevice for %s",
			    fu_dfu_device_get_platform_id(self));
		return FALSE;
	}

	/* the device has no DFU runtime, so cheat */
	if (priv->state == FU_DFU_STATE_APP_IDLE &&
	    fu_device_has_private_flag(FU_DEVICE(self), FU_DFU_DEVICE_FLAG_NO_DFU_RUNTIME)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "not supported as no DFU runtime");
		return FALSE;
	}

	/* ensure interface is claimed */
	if (!fu_dfu_device_ensure_interface(self, error))
		return FALSE;

	if (!g_usb_device_control_transfer(usb_device,
					   G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					   G_USB_DEVICE_REQUEST_TYPE_CLASS,
					   G_USB_DEVICE_RECIPIENT_INTERFACE,
					   FU_DFU_REQUEST_CLRSTATUS,
					   0,
					   priv->iface_number,
					   NULL,
					   0,
					   NULL,
					   priv->timeout_ms,
					   NULL, /* cancellable */
					   &error_local)) {
		/* refresh the error code */
		fu_dfu_device_error_fixup(self, &error_local);
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "cannot clear status on the device: %s",
			    error_local->message);
		return FALSE;
	}
	return TRUE;
}

/**
 * fu_dfu_device_get_interface:
 * @self: a #FuDfuDevice
 *
 * Gets the interface number.
 **/
guint8
fu_dfu_device_get_interface(FuDfuDevice *self)
{
	FuDfuDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_DFU_DEVICE(self), 0xff);
	return priv->iface_number;
}

/**
 * fu_dfu_device_open:
 * @self: a #FuDfuDevice
 * @error: (nullable): optional return location for an error
 *
 * Opens a DFU-capable device.
 *
 * Returns: %TRUE for success
 **/
static gboolean
fu_dfu_device_open(FuDevice *device, GError **error)
{
	FuDfuDevice *self = FU_DFU_DEVICE(device);
	FuDfuDevicePrivate *priv = GET_PRIVATE(self);
	GPtrArray *targets = fu_dfu_device_get_targets(self);

	g_return_val_if_fail(FU_IS_DFU_DEVICE(device), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* FuUsbDevice->open */
	if (!FU_DEVICE_CLASS(fu_dfu_device_parent_class)->open(device, error))
		return FALSE;

	/* the device has no DFU runtime, so cheat */
	if (priv->state == FU_DFU_STATE_APP_IDLE &&
	    fu_device_has_private_flag(FU_DEVICE(self), FU_DFU_DEVICE_FLAG_NO_DFU_RUNTIME)) {
		fu_dfu_device_set_state(self, FU_DFU_STATE_APP_IDLE);
		priv->status = FU_DFU_STATUS_OK;
	}

	/* GD32VF103 encodes the serial number in UTF-8 (rather than UTF-16)
	 * and also uses the first two bytes as the model identifier */
	if (fu_device_has_private_flag(FU_DEVICE(self), FU_DFU_DEVICE_FLAG_GD32)) {
#if G_USB_CHECK_VERSION(0, 3, 6)
		GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(device));
		const guint8 *buf;
		gsize bufsz = 0;
		guint16 langid = G_USB_DEVICE_LANGID_ENGLISH_UNITED_STATES;
		guint8 idx = g_usb_device_get_serial_number_index(usb_device);
		g_autofree gchar *chip_id = NULL;
		g_autofree gchar *serial_str = NULL;
		g_autoptr(GBytes) serial_blob = NULL;
		serial_blob =
		    g_usb_device_get_string_descriptor_bytes(usb_device, idx, langid, error);
		if (serial_blob == NULL)
			return FALSE;
		if (g_getenv("FWUPD_DFU_VERBOSE") != NULL)
			fu_common_dump_bytes(G_LOG_DOMAIN, "GD32 serial", serial_blob);
		buf = g_bytes_get_data(serial_blob, &bufsz);
		if (bufsz < 2) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "GD32 serial number invalid");
			return FALSE;
		}

		/* ID is first two bytes */
		chip_id = g_strdup_printf("%02x%02x", buf[0], buf[1]);
		fu_dfu_device_set_chip_id(self, chip_id);

		/* serial number follows */
		serial_str = g_strndup((const gchar *)buf + 2, bufsz - 2);
		fu_device_set_serial(FU_DEVICE(device), serial_str);
#else
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "GUsb version too old to support GD32, "
				    "fwupd needs to be rebuilt against 0.3.6 or later");
		return FALSE;
#endif
	}

	/* set up target ready for use */
	for (guint j = 0; j < targets->len; j++) {
		FuDfuTarget *target = g_ptr_array_index(targets, j);
		if (!fu_dfu_target_setup(target, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

/**
 * fu_dfu_device_close:
 * @self: a #FuDfuDevice
 * @error: (nullable): optional return location for an error
 *
 * Closes a DFU device.
 *
 * Returns: %TRUE for success
 **/
static gboolean
fu_dfu_device_close(FuDevice *device, GError **error)
{
	FuDfuDevice *self = FU_DFU_DEVICE(device);
	FuDfuDevicePrivate *priv = GET_PRIVATE(self);
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(device));

	/* release interface */
	if (priv->claimed_interface) {
		g_autoptr(GError) error_local = NULL;
		if (!g_usb_device_release_interface(usb_device,
						    (gint)priv->iface_number,
						    0,
						    &error_local)) {
			if (!g_error_matches(error_local,
					     G_USB_DEVICE_ERROR,
					     G_USB_DEVICE_ERROR_NO_DEVICE)) {
				g_warning("failed to release interface: %s", error_local->message);
			}
		}
		priv->claimed_interface = FALSE;
	}

	/* FuUsbDevice->close */
	return FU_DEVICE_CLASS(fu_dfu_device_parent_class)->close(device, error);
}

static gboolean
fu_dfu_device_probe(FuDevice *device, GError **error)
{
	FuDfuDevice *self = FU_DFU_DEVICE(device);
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(device));

	/* FuUsbDevice->probe */
	if (!FU_DEVICE_CLASS(fu_dfu_device_parent_class)->probe(device, error))
		return FALSE;

	/* add all the targets */
	if (!fu_dfu_device_add_targets(self, error)) {
		g_prefix_error(error,
			       "%04x:%04x is not supported: ",
			       g_usb_device_get_vid(usb_device),
			       g_usb_device_get_pid(usb_device));
		return FALSE;
	}

	/* check capabilities */
	if (!fu_dfu_device_can_download(self)) {
		g_debug("%04x:%04x is missing download capability",
			g_usb_device_get_vid(usb_device),
			g_usb_device_get_pid(usb_device));
	}

	/* hardware from Jabra literally reboots if you try to retry a failed
	 * write -- there's no way to avoid blocking the daemon like this... */
	if (fu_device_has_private_flag(FU_DEVICE(self), FU_DFU_DEVICE_FLAG_ATTACH_EXTRA_RESET)) {
		g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
		fu_progress_sleep(progress, 10000);
	}

	/* success */
	return TRUE;
}

gboolean
fu_dfu_device_reset(FuDfuDevice *self, FuProgress *progress, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(self));
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GTimer) timer = g_timer_new();

	g_return_val_if_fail(FU_IS_DFU_DEVICE(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* no backing USB device */
	if (usb_device == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "failed to reset: no GUsbDevice for %s",
			    fu_dfu_device_get_platform_id(self));
		return FALSE;
	}

	if (!g_usb_device_reset(usb_device, &error_local)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "cannot reset USB device: %s [%i]",
			    error_local->message,
			    error_local->code);
		return FALSE;
	}
	g_debug("reset took %.2lfms", g_timer_elapsed(timer, NULL) * 1000);
	return TRUE;
}

static gboolean
fu_dfu_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuDfuDevice *self = FU_DFU_DEVICE(device);
	FuDfuDevicePrivate *priv = GET_PRIVATE(self);
	g_autoptr(FuDfuTarget) target = NULL;

	g_return_val_if_fail(FU_IS_DFU_DEVICE(device), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* already in runtime mode */
	if (!fu_dfu_device_refresh_and_clear(self, error))
		return FALSE;
	if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER))
		return TRUE;

	/* handle weirdness */
	if (fu_device_has_private_flag(FU_DEVICE(self), FU_DFU_DEVICE_FLAG_DETACH_FOR_ATTACH)) {
		if (!fu_dfu_device_request_detach(self, progress, error))
			return FALSE;
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
		return TRUE;
	}

	/* handle m-stack DFU bootloaders */
	if (!priv->done_upload_or_download &&
	    fu_device_has_private_flag(FU_DEVICE(self),
				       FU_DFU_DEVICE_FLAG_ATTACH_UPLOAD_DOWNLOAD)) {
		g_autoptr(GBytes) chunk = NULL;
		g_autoptr(FuDfuTarget) target_zero = NULL;
		g_debug("doing dummy upload to work around m-stack quirk");
		target_zero = fu_dfu_device_get_target_by_alt_setting(self, 0, error);
		if (target_zero == NULL)
			return FALSE;
		chunk = fu_dfu_target_upload_chunk(target_zero, 0, 0, progress, error);
		if (chunk == NULL)
			return FALSE;
	}

	/* get default target */
	target = fu_dfu_device_get_target_by_alt_setting(self, 0, error);
	if (target == NULL)
		return FALSE;

	/* normal DFU mode just needs a bus reset */
	if (fu_device_has_private_flag(FU_DEVICE(self), FU_DFU_DEVICE_FLAG_NO_BUS_RESET_ATTACH) &&
	    fu_dfu_device_has_attribute(self, FU_DFU_DEVICE_ATTR_WILL_DETACH)) {
		g_debug("Bus reset is not required. Device will reboot to normal");
	} else if (!fu_dfu_target_attach(target, progress, error)) {
		g_prefix_error(error, "failed to attach target: ");
		return FALSE;
	}

	/* there is no USB runtime whatsoever */
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_WILL_DISAPPEAR))
		return TRUE;

	/* success */
	priv->force_version = 0x0;
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

/**
 * fu_dfu_device_upload:
 * @self: a #FuDfuDevice
 * @flags: DFU target flags, e.g. %DFU_TARGET_TRANSFER_FLAG_VERIFY
 * @error: (nullable): optional return location for an error
 *
 * Uploads firmware from the target to the host.
 *
 * Returns: (transfer full): the uploaded firmware, or %NULL for error
 **/
FuFirmware *
fu_dfu_device_upload(FuDfuDevice *self,
		     FuProgress *progress,
		     FuDfuTargetTransferFlags flags,
		     GError **error)
{
	FuDfuDevicePrivate *priv = GET_PRIVATE(self);
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(self));
	gboolean use_dfuse = FALSE;
	g_autoptr(FuFirmware) firmware = NULL;

	/* no backing USB device */
	if (usb_device == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "failed to upload: no GUsbDevice for %s",
			    fu_dfu_device_get_platform_id(self));
		return NULL;
	}

	/* ensure interface is claimed */
	if (!fu_dfu_device_ensure_interface(self, error))
		return NULL;

	/* choose the most appropriate type */
	for (guint i = 0; i < priv->targets->len; i++) {
		FuDfuTarget *target = g_ptr_array_index(priv->targets, i);
		if (fu_dfu_target_get_alt_name(target, NULL) != NULL || i > 0) {
			use_dfuse = TRUE;
			break;
		}
	}
	if (use_dfuse) {
		firmware = fu_dfuse_firmware_new();
		g_debug("switching to DefuSe automatically");
	} else {
		firmware = fu_dfu_firmware_new();
	}
	fu_dfu_firmware_set_vid(FU_DFU_FIRMWARE(firmware), priv->runtime_vid);
	fu_dfu_firmware_set_pid(FU_DFU_FIRMWARE(firmware), priv->runtime_pid);
	fu_dfu_firmware_set_release(FU_DFU_FIRMWARE(firmware), 0xffff);

	/* upload from each target */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, priv->targets->len);
	for (guint i = 0; i < priv->targets->len; i++) {
		FuDfuTarget *target;
		const gchar *alt_name;

		/* upload to target and proxy signals */
		target = g_ptr_array_index(priv->targets, i);

		/* ignore some target types */
		alt_name = fu_dfu_target_get_alt_name_for_display(target, NULL);
		if (g_strcmp0(alt_name, "Option Bytes") == 0) {
			g_debug("ignoring target %s", alt_name);
			continue;
		}
		if (!fu_dfu_target_upload(target,
					  firmware,
					  fu_progress_get_child(progress),
					  DFU_TARGET_TRANSFER_FLAG_NONE,
					  error))
			return NULL;
		fu_progress_step_done(progress);
	}

	/* do not do the dummy upload for quirked devices */
	priv->done_upload_or_download = TRUE;

	/* success */
	return g_object_ref(firmware);
}

static gboolean
fu_dfu_device_id_compatible(guint16 id_file, guint16 id_runtime, guint16 id_dev)
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

static gsize
fu_dfu_device_calculate_chunks_size(GPtrArray *chunks)
{
	gsize total = 0;
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		total += fu_chunk_get_data_sz(chk);
	}
	return total;
}

static gboolean
fu_dfu_device_download(FuDfuDevice *self,
		       FuFirmware *firmware,
		       FuProgress *progress,
		       FuDfuTargetTransferFlags flags,
		       GError **error)
{
	FuDfuDevicePrivate *priv = GET_PRIVATE(self);
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(self));
	gboolean ret;
	g_autoptr(GPtrArray) images = NULL;
	guint16 firmware_pid = 0xffff;
	guint16 firmware_vid = 0xffff;

	/* no backing USB device */
	if (usb_device == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "failed to download: no GUsbDevice for %s",
			    fu_dfu_device_get_platform_id(self));
		return FALSE;
	}

	/* ensure interface is claimed */
	if (!fu_dfu_device_ensure_interface(self, error))
		return FALSE;

	/* firmware supports footer? */
	if (FU_IS_DFU_FIRMWARE(firmware)) {
		firmware_vid = fu_dfu_firmware_get_vid(FU_DFU_FIRMWARE(firmware));
		firmware_pid = fu_dfu_firmware_get_pid(FU_DFU_FIRMWARE(firmware));
	} else {
		flags |= DFU_TARGET_TRANSFER_FLAG_WILDCARD_VID;
		flags |= DFU_TARGET_TRANSFER_FLAG_WILDCARD_PID;
	}

	/* do we allow wildcard VID:PID matches */
	if ((flags & DFU_TARGET_TRANSFER_FLAG_WILDCARD_VID) == 0) {
		if (firmware_vid == 0xffff) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "firmware vendor ID not specified");
			return FALSE;
		}
	}
	if ((flags & DFU_TARGET_TRANSFER_FLAG_WILDCARD_PID) == 0) {
		if (firmware_pid == 0xffff) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "firmware product ID not specified");
			return FALSE;
		}
	}

	/* check vendor matches */
	if (priv->runtime_vid != 0xffff) {
		if (!fu_dfu_device_id_compatible(firmware_vid,
						 priv->runtime_vid,
						 fu_usb_device_get_vid(FU_USB_DEVICE(self)))) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "vendor ID incorrect, expected 0x%04x "
				    "got 0x%04x and 0x%04x\n",
				    firmware_vid,
				    priv->runtime_vid,
				    fu_usb_device_get_vid(FU_USB_DEVICE(self)));
			return FALSE;
		}
	}

	/* check product matches */
	if (priv->runtime_pid != 0xffff) {
		if (!fu_dfu_device_id_compatible(firmware_pid,
						 priv->runtime_pid,
						 fu_usb_device_get_pid(FU_USB_DEVICE(self)))) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "product ID incorrect, expected 0x%04x "
				    "got 0x%04x and 0x%04x",
				    firmware_pid,
				    priv->runtime_pid,
				    fu_usb_device_get_pid(FU_USB_DEVICE(self)));
			return FALSE;
		}
	}

	/* download each target */
	images = fu_firmware_get_images(firmware);
	if (images->len == 0)
		g_ptr_array_add(images, g_object_ref(firmware));
	fu_progress_set_id(progress, G_STRLOC);
	for (guint i = 0; i < images->len; i++) {
		FuFirmware *image = g_ptr_array_index(images, i);
		g_autoptr(GPtrArray) chunks = fu_firmware_get_chunks(image, error);
		if (chunks == NULL)
			return FALSE;
		fu_progress_add_step(progress,
				     FWUPD_STATUS_DEVICE_WRITE,
				     fu_dfu_device_calculate_chunks_size(chunks));
	}
	for (guint i = 0; i < images->len; i++) {
		FuFirmware *image = g_ptr_array_index(images, i);
		FuDfuTargetTransferFlags flags_local = DFU_TARGET_TRANSFER_FLAG_NONE;
		const gchar *alt_name;
		guint8 alt;
		g_autoptr(FuDfuTarget) target_tmp = NULL;
		g_autoptr(GError) error_local = NULL;

		alt = fu_firmware_get_idx(image);
		target_tmp = fu_dfu_device_get_target_by_alt_setting(self, alt, error);
		if (target_tmp == NULL)
			return FALSE;

		/* we don't actually need to print this */
		alt_name = fu_dfu_target_get_alt_name(target_tmp, &error_local);
		if (alt_name == NULL) {
			if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND)) {
				alt_name = "unknown";
			} else {
				g_propagate_error(error, g_steal_pointer(&error_local));
				return FALSE;
			}
		}
		g_debug("downloading to target: %s", alt_name);

		/* download onto target */
		if (flags & DFU_TARGET_TRANSFER_FLAG_VERIFY)
			flags_local = DFU_TARGET_TRANSFER_FLAG_VERIFY;
		if (!FU_IS_DFU_FIRMWARE(firmware) ||
		    fu_dfu_firmware_get_version(FU_DFU_FIRMWARE(firmware)) == 0x0)
			flags_local |= DFU_TARGET_TRANSFER_FLAG_ADDR_HEURISTIC;
		ret = fu_dfu_target_download(target_tmp,
					     image,
					     fu_progress_get_child(progress),
					     flags_local,
					     error);
		if (!ret)
			return FALSE;
		fu_progress_step_done(progress);
	}

	/* do not do the dummy upload for quirked devices */
	priv->done_upload_or_download = TRUE;

	/* success */
	return TRUE;
}

void
fu_dfu_device_error_fixup(FuDfuDevice *self, GError **error)
{
	FuDfuDevicePrivate *priv = GET_PRIVATE(self);

	/* sad panda */
	if (error == NULL)
		return;

	/* not the right error to query */
	if (!g_error_matches(*error, G_USB_DEVICE_ERROR, G_USB_DEVICE_ERROR_NOT_SUPPORTED))
		return;

	/* get the status */
	if (!fu_dfu_device_refresh(self, NULL))
		return;

	/* not in an error state */
	if (priv->state != FU_DFU_STATE_DFU_ERROR)
		return;

	/* prefix the error */
	switch (priv->status) {
	case FU_DFU_STATUS_OK:
		/* ignore */
		break;
	case FU_DFU_STATUS_ERR_VENDOR:
		g_prefix_error(error, "read protection is active: ");
		break;
	default:
		g_prefix_error(error,
			       "[%s,%s]: ",
			       fu_dfu_state_to_string(priv->state),
			       fu_dfu_status_to_string(priv->status));
		break;
	}
}

static GBytes *
fu_dfu_device_dump_firmware(FuDevice *device, FuProgress *progress, GError **error)
{
	FuDfuDevice *self = FU_DFU_DEVICE(device);
	g_autoptr(FuFirmware) firmware = NULL;

	/* get data from hardware */
	g_debug("uploading from device->host");
	if (!fu_dfu_device_refresh_and_clear(self, error))
		return NULL;
	firmware = fu_dfu_device_upload(self, progress, DFU_TARGET_TRANSFER_FLAG_NONE, error);
	if (firmware == NULL)
		return NULL;

	/* get the checksum */
	return fu_firmware_write(firmware, error);
}

static FuFirmware *
fu_dfu_device_prepare_firmware(FuDevice *device,
			       GBytes *fw,
			       FwupdInstallFlags flags,
			       GError **error)
{
	return fu_firmware_new_from_gtypes(fw,
					   flags,
					   error,
					   FU_TYPE_IHEX_FIRMWARE,
					   FU_TYPE_DFUSE_FIRMWARE,
					   FU_TYPE_DFU_FIRMWARE,
					   FU_TYPE_FIRMWARE,
					   G_TYPE_INVALID);
}

static gboolean
fu_dfu_device_write_firmware(FuDevice *device,
			     FuFirmware *firmware,
			     FuProgress *progress,
			     FwupdInstallFlags flags,
			     GError **error)
{
	FuDfuDevice *self = FU_DFU_DEVICE(device);
	FuDfuTargetTransferFlags transfer_flags = DFU_TARGET_TRANSFER_FLAG_VERIFY;

	/* open it */
	if (!fu_dfu_device_refresh_and_clear(self, error))
		return FALSE;
	if (flags & FWUPD_INSTALL_FLAG_IGNORE_VID_PID) {
		transfer_flags |= DFU_TARGET_TRANSFER_FLAG_WILDCARD_VID;
		transfer_flags |= DFU_TARGET_TRANSFER_FLAG_WILDCARD_PID;
	}

	/* hit hardware */
	return fu_dfu_device_download(self, firmware, progress, transfer_flags, error);
}

static gboolean
fu_dfu_device_set_quirk_kv(FuDevice *device, const gchar *key, const gchar *value, GError **error)
{
	FuDfuDevice *self = FU_DFU_DEVICE(device);
	FuDfuDevicePrivate *priv = GET_PRIVATE(self);
	guint64 tmp = 0;

	if (g_strcmp0(key, FU_QUIRKS_DFU_FORCE_VERSION) == 0) {
		if (value != NULL) {
			gsize valuesz = strlen(value);
			return fu_firmware_strparse_uint16_safe(value,
								valuesz,
								0,
								&priv->force_version,
								error);
		}
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "invalid DFU version");
		return FALSE;
	}
	if (g_strcmp0(key, "DfuForceTimeout") == 0) {
		if (!fu_common_strtoull_full(value, &tmp, 0, G_MAXUINT, error))
			return FALSE;
		priv->timeout_ms = tmp;
		return TRUE;
	}
	if (g_strcmp0(key, "DfuForceTransferSize") == 0) {
		if (!fu_common_strtoull_full(value, &tmp, 0, G_MAXUINT16, error))
			return FALSE;
		priv->force_transfer_size = tmp;
		return TRUE;
	}
	if (g_strcmp0(key, "DfuAltName") == 0) {
		fu_dfu_device_set_chip_id(self, value);
		return TRUE;
	}

	/* failed */
	g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "quirk key not supported");
	return FALSE;
}

/**
 * fu_dfu_device_get_attributes_as_string: (skip)
 * @self: a #FuDfuDevice
 *
 * Gets a string describing the attributes for a device.
 *
 * Returns: a string, possibly empty
 **/
gchar *
fu_dfu_device_get_attributes_as_string(FuDfuDevice *self)
{
	FuDfuDevicePrivate *priv = GET_PRIVATE(self);
	GString *str;

	/* just append to a string */
	str = g_string_new("");
	if (priv->attributes & FU_DFU_DEVICE_ATTR_CAN_DOWNLOAD)
		g_string_append_printf(str, "can-download|");
	if (priv->attributes & FU_DFU_DEVICE_ATTR_CAN_UPLOAD)
		g_string_append_printf(str, "can-upload|");
	if (priv->attributes & FU_DFU_DEVICE_ATTR_MANIFEST_TOL)
		g_string_append_printf(str, "manifest-tol|");
	if (priv->attributes & FU_DFU_DEVICE_ATTR_WILL_DETACH)
		g_string_append_printf(str, "will-detach|");
	if (priv->attributes & FU_DFU_DEVICE_ATTR_CAN_ACCELERATE)
		g_string_append_printf(str, "can-accelerate|");

	/* remove trailing pipe */
	g_string_truncate(str, str->len - 1);
	return g_string_free(str, FALSE);
}

static void
fu_dfu_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 1); /* detach */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 88);	/* write */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 1); /* attach */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 10);	/* reload */
}

static void
fu_dfu_device_finalize(GObject *object)
{
	FuDfuDevice *self = FU_DFU_DEVICE(object);
	FuDfuDevicePrivate *priv = GET_PRIVATE(self);

	g_free(priv->chip_id);
	g_ptr_array_unref(priv->targets);

	G_OBJECT_CLASS(fu_dfu_device_parent_class)->finalize(object);
}

static void
fu_dfu_device_class_init(FuDfuDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->set_quirk_kv = fu_dfu_device_set_quirk_kv;
	klass_device->to_string = fu_dfu_device_to_string;
	klass_device->dump_firmware = fu_dfu_device_dump_firmware;
	klass_device->write_firmware = fu_dfu_device_write_firmware;
	klass_device->prepare_firmware = fu_dfu_device_prepare_firmware;
	klass_device->attach = fu_dfu_device_attach;
	klass_device->detach = fu_dfu_device_detach;
	klass_device->reload = fu_dfu_device_reload;
	klass_device->open = fu_dfu_device_open;
	klass_device->close = fu_dfu_device_close;
	klass_device->probe = fu_dfu_device_probe;
	klass_device->set_progress = fu_dfu_device_set_progress;
	object_class->finalize = fu_dfu_device_finalize;
}

static void
fu_dfu_device_init(FuDfuDevice *self)
{
	FuDfuDevicePrivate *priv = GET_PRIVATE(self);
	priv->iface_number = 0xff;
	priv->runtime_pid = 0xffff;
	priv->runtime_vid = 0xffff;
	priv->runtime_release = 0xffff;
	priv->state = FU_DFU_STATE_APP_IDLE;
	priv->status = FU_DFU_STATUS_OK;
	priv->targets = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	priv->timeout_ms = 1500;
	priv->transfer_size = 64;
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_ADD_COUNTERPART_GUIDS);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_REPLUG_MATCH_GUID);
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);

	fu_device_register_private_flag(FU_DEVICE(self),
					FU_DFU_DEVICE_FLAG_ATTACH_EXTRA_RESET,
					"attach-extra-reset");
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_DFU_DEVICE_FLAG_ATTACH_UPLOAD_DOWNLOAD,
					"attach-upload-download");
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_DFU_DEVICE_FLAG_FORCE_DFU_MODE,
					"force-dfu-mode");
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_DFU_DEVICE_FLAG_IGNORE_POLLTIMEOUT,
					"ignore-polltimeout");
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_DFU_DEVICE_FLAG_IGNORE_RUNTIME,
					"ignore-runtime");
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_DFU_DEVICE_FLAG_IGNORE_UPLOAD,
					"ignore-upload");
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_DFU_DEVICE_FLAG_NO_DFU_RUNTIME,
					"no-dfu-runtime");
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_DFU_DEVICE_FLAG_NO_GET_STATUS_UPLOAD,
					"no-get-status-upload");
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_DFU_DEVICE_FLAG_NO_PID_CHANGE,
					"no-pid-change");
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_DFU_DEVICE_FLAG_USE_ANY_INTERFACE,
					"use-any-interface");
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_DFU_DEVICE_FLAG_USE_ATMEL_AVR,
					"use-atmel-avr");
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_DFU_DEVICE_FLAG_USE_PROTOCOL_ZERO,
					"use-protocol-zero");
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_DFU_DEVICE_FLAG_LEGACY_PROTOCOL,
					"legacy-protocol");
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_DFU_DEVICE_FLAG_DETACH_FOR_ATTACH,
					"detach-for-attach");
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_DFU_DEVICE_FLAG_ABSENT_SECTOR_SIZE,
					"absent-sector-size");
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_DFU_DEVICE_FLAG_MANIFEST_POLL,
					"manifest-poll");
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_DFU_DEVICE_FLAG_NO_BUS_RESET_ATTACH,
					"no-bus-reset-attach");
	fu_device_register_private_flag(FU_DEVICE(self), FU_DFU_DEVICE_FLAG_GD32, "gd32");
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_DFU_DEVICE_FLAG_ALLOW_ZERO_POLLTIMEOUT,
					"allow-zero-polltimeout");
}
