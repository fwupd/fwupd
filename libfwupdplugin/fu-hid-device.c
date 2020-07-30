/*
 * Copyright (C) 2017-2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN				"FuHidDevice"

#include "config.h"

#include "fu-hid-device.h"

#define FU_HID_REPORT_GET				0x01
#define FU_HID_REPORT_SET				0x09

#define FU_HID_REPORT_TYPE_INPUT			0x01
#define FU_HID_REPORT_TYPE_OUTPUT			0x02
#define FU_HID_REPORT_TYPE_FEATURE			0x03

/**
 * SECTION:fu-hid-device
 * @short_description: a HID device
 *
 * An object that represents a HID device.
 *
 * See also: #FuDevice
 */

typedef struct
{
	FuUsbDevice		*usb_device;
	guint8			 interface;
	gboolean		 interface_autodetect;
} FuHidDevicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (FuHidDevice, fu_hid_device, FU_TYPE_USB_DEVICE)

enum {
	PROP_0,
	PROP_INTERFACE,
	PROP_LAST
};

#define GET_PRIVATE(o) (fu_hid_device_get_instance_private (o))

static void
fu_hid_device_get_property (GObject *object, guint prop_id,
			    GValue *value, GParamSpec *pspec)
{
	FuHidDevice *device = FU_HID_DEVICE (object);
	FuHidDevicePrivate *priv = GET_PRIVATE (device);
	switch (prop_id) {
	case PROP_INTERFACE:
		g_value_set_uint (value, priv->interface);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
fu_hid_device_set_property (GObject *object, guint prop_id,
			    const GValue *value, GParamSpec *pspec)
{
	FuHidDevice *device = FU_HID_DEVICE (object);
	switch (prop_id) {
	case PROP_INTERFACE:
		fu_hid_device_set_interface (device, g_value_get_uint (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static gboolean
fu_hid_device_open (FuUsbDevice *device, GError **error)
{
	FuHidDevice *self = FU_HID_DEVICE (device);
	FuHidDeviceClass *klass = FU_HID_DEVICE_GET_CLASS (device);
	FuHidDevicePrivate *priv = GET_PRIVATE (self);
	GUsbDevice *usb_device = fu_usb_device_get_dev (device);

	/* auto-detect */
	if (priv->interface_autodetect) {
		g_autoptr(GPtrArray) ifaces = NULL;
		ifaces = g_usb_device_get_interfaces (usb_device, error);
		if (ifaces == NULL)
			return FALSE;
		for (guint i = 0; i < ifaces->len; i++) {
			GUsbInterface *iface = g_ptr_array_index (ifaces, i);
			if (g_usb_interface_get_class (iface) == G_USB_DEVICE_CLASS_HID) {
				priv->interface = g_usb_interface_get_number (iface);
				priv->interface_autodetect = FALSE;
				break;
			}
		}
		if (priv->interface_autodetect) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_NOT_SUPPORTED,
					     "could not autodetect HID interface");
			return FALSE;
		}
		g_debug ("autodetected HID interface of 0x%02x", priv->interface);
	}

	/* claim */
	if (!g_usb_device_claim_interface (usb_device, priv->interface,
					   G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
					   error)) {
		g_prefix_error (error, "failed to claim HID interface: ");
		return FALSE;
	}

	/* subclassed */
	if (klass->open != NULL) {
		if (!klass->open (self, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_hid_device_close (FuUsbDevice *device, GError **error)
{
	FuHidDevice *self = FU_HID_DEVICE (device);
	FuHidDeviceClass *klass = FU_HID_DEVICE_GET_CLASS (device);
	FuHidDevicePrivate *priv = GET_PRIVATE (self);
	GUsbDevice *usb_device = fu_usb_device_get_dev (device);

	/* subclassed */
	if (klass->close != NULL) {
		if (!klass->close (self, error))
			return FALSE;
	}

	/* release */
	if (!g_usb_device_release_interface (usb_device, priv->interface,
					     G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
					     error)) {
		g_prefix_error (error, "failed to release HID interface: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

/**
 * fu_hid_device_set_interface:
 * @self: A #FuHidDevice
 * @interface: An interface number, e.g. 0x03
 *
 * Sets the HID USB interface number.
 *
 * In most cases the HID interface is auto-detected, but this function can be
 * used where there are multiple HID interfaces or where the device USB
 * interface descriptor is invalid.
 *
 * Since: 1.4.0
 **/
void
fu_hid_device_set_interface (FuHidDevice *self, guint8 interface)
{
	FuHidDevicePrivate *priv = GET_PRIVATE (self);
	g_return_if_fail (FU_HID_DEVICE (self));
	priv->interface = interface;
	priv->interface_autodetect = FALSE;
}

/**
 * fu_hid_device_get_interface:
 * @self: A #FuHidDevice
 *
 * Gets the HID USB interface number.
 *
 * Returns: integer
 *
 * Since: 1.4.0
 **/
guint8
fu_hid_device_get_interface (FuHidDevice *self)
{
	FuHidDevicePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_HID_DEVICE (self), 0xff);
	return priv->interface;
}

/**
 * fu_hid_device_set_report:
 * @self: A #FuHidDevice
 * @value: low byte of wValue
 * @buf: (nullable): a mutable buffer of data to send
 * @bufsz: Size of @buf
 * @timeout: timeout in ms
 * @flags: #FuHidDeviceFlags e.g. %FU_HID_DEVICE_FLAG_ALLOW_TRUNC
 * @error: a #GError or %NULL
 *
 * Calls SetReport on the hardware.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.4.0
 **/
gboolean
fu_hid_device_set_report (FuHidDevice *self,
			  guint8 value,
			  guint8 *buf,
			  gsize bufsz,
			  guint timeout,
			  FuHidDeviceFlags flags,
			  GError **error)
{
	FuHidDevicePrivate *priv = GET_PRIVATE (self);
	GUsbDevice *usb_device;
	gsize actual_len = 0;
	guint16 wvalue = (FU_HID_REPORT_TYPE_OUTPUT << 8) | value;

	/* special case */
	if (flags & FU_HID_DEVICE_FLAG_IS_FEATURE)
		wvalue = (FU_HID_REPORT_TYPE_FEATURE << 8) | value;

	g_return_val_if_fail (FU_HID_DEVICE (self), FALSE);
	g_return_val_if_fail (buf != NULL, FALSE);
	g_return_val_if_fail (bufsz != 0, FALSE);

	if (g_getenv ("FU_HID_DEVICE_VERBOSE") != NULL)
		fu_common_dump_raw (G_LOG_DOMAIN, "HID::SetReport", buf, bufsz);
	usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));
	if (!g_usb_device_control_transfer (usb_device,
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_CLASS,
					    G_USB_DEVICE_RECIPIENT_INTERFACE,
					    FU_HID_REPORT_SET,
					    wvalue, priv->interface,
					    buf, bufsz,
					    &actual_len,
					    timeout,
					    NULL, error)) {
		g_prefix_error (error, "failed to SetReport: ");
		return FALSE;
	}
	if ((flags & FU_HID_DEVICE_FLAG_ALLOW_TRUNC) == 0 && actual_len != bufsz) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
			     "wrote %" G_GSIZE_FORMAT ", requested %" G_GSIZE_FORMAT " bytes",
			     actual_len, bufsz);
		return FALSE;
	}
	return TRUE;
}

/**
 * fu_hid_device_get_report:
 * @self: A #FuHidDevice
 * @value: low byte of wValue
 * @buf: (nullable): a mutable buffer of data to send
 * @bufsz: Size of @buf
 * @timeout: timeout in ms
 * @flags: #FuHidDeviceFlags e.g. %FU_HID_DEVICE_FLAG_ALLOW_TRUNC
 * @error: a #GError or %NULL
 *
 * Calls GetReport on the hardware.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.4.0
 **/
gboolean
fu_hid_device_get_report (FuHidDevice *self,
			  guint8 value,
			  guint8 *buf,
			  gsize bufsz,
			  guint timeout,
			  FuHidDeviceFlags flags,
			  GError **error)
{
	FuHidDevicePrivate *priv = GET_PRIVATE (self);
	GUsbDevice *usb_device;
	gsize actual_len = 0;
	guint16 wvalue = (FU_HID_REPORT_TYPE_INPUT << 8) | value;

	g_return_val_if_fail (FU_HID_DEVICE (self), FALSE);
	g_return_val_if_fail (buf != NULL, FALSE);
	g_return_val_if_fail (bufsz != 0, FALSE);

	/* special case */
	if (flags & FU_HID_DEVICE_FLAG_IS_FEATURE)
		wvalue = (FU_HID_REPORT_TYPE_FEATURE << 8) | value;

	if (g_getenv ("FU_HID_DEVICE_VERBOSE") != NULL)
		fu_common_dump_raw (G_LOG_DOMAIN, "HID::GetReport", buf, actual_len);
	usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));
	if (!g_usb_device_control_transfer (usb_device,
					    G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
					    G_USB_DEVICE_REQUEST_TYPE_CLASS,
					    G_USB_DEVICE_RECIPIENT_INTERFACE,
					    FU_HID_REPORT_GET,
					    wvalue, priv->interface,
					    buf, bufsz,
					    &actual_len, /* actual length */
					    timeout,
					    NULL, error)) {
		g_prefix_error (error, "failed to GetReport: ");
		return FALSE;
	}
	if (g_getenv ("FU_HID_DEVICE_VERBOSE") != NULL)
		fu_common_dump_raw (G_LOG_DOMAIN, "HID::GetReport", buf, actual_len);
	if ((flags & FU_HID_DEVICE_FLAG_ALLOW_TRUNC) == 0 && actual_len != bufsz) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
			     "read %" G_GSIZE_FORMAT ", requested %" G_GSIZE_FORMAT " bytes",
			     actual_len, bufsz);
		return FALSE;
	}
	return TRUE;
}

static void
fu_hid_device_init (FuHidDevice *self)
{
	FuHidDevicePrivate *priv = GET_PRIVATE (self);
	priv->interface_autodetect = TRUE;
}

/**
 * fu_hid_device_new:
 * @usb_device: A #GUsbDevice
 *
 * Creates a new #FuHidDevice.
 *
 * Returns: (transfer full): a #FuHidDevice
 *
 * Since: 1.4.0
 **/
FuHidDevice *
fu_hid_device_new (GUsbDevice *usb_device)
{
	FuHidDevice *device = g_object_new (FU_TYPE_HID_DEVICE, NULL);
	fu_usb_device_set_dev (FU_USB_DEVICE (device), usb_device);
	return FU_HID_DEVICE (device);
}

static void
fu_hid_device_class_init (FuHidDeviceClass *klass)
{
	FuUsbDeviceClass *klass_usb_device = FU_USB_DEVICE_CLASS (klass);
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GParamSpec *pspec;

	object_class->get_property = fu_hid_device_get_property;
	object_class->set_property = fu_hid_device_set_property;
	klass_usb_device->open = fu_hid_device_open;
	klass_usb_device->close = fu_hid_device_close;

	pspec = g_param_spec_uint ("interface", NULL, NULL,
				   0x00, 0xff, 0x00,
				   G_PARAM_READWRITE |
				   G_PARAM_STATIC_NAME);
	g_object_class_install_property (object_class, PROP_INTERFACE, pspec);
}
