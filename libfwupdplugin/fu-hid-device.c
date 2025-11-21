/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuHidDevice"

#include "config.h"

#include "fu-dump.h"
#include "fu-hid-device.h"
#include "fu-string.h"
#include "fu-usb-device-private.h"
#include "fu-usb-endpoint.h"

#define FU_HID_REPORT_GET 0x01
#define FU_HID_REPORT_SET 0x09

#define FU_HID_REPORT_TYPE_INPUT   0x01
#define FU_HID_REPORT_TYPE_OUTPUT  0x02
#define FU_HID_REPORT_TYPE_FEATURE 0x03

#define FU_HID_DEVICE_RETRIES 10

/**
 * FuHidDevice:
 *
 * A Human Interface Device (HID) device.
 *
 * See also: [class@FuDevice], [class@FuUsbDevice]
 */

typedef struct {
	guint8 interface;
	guint8 ep_addr_in;  /* only for _USE_INTERRUPT_TRANSFER */
	guint8 ep_addr_out; /* only for _USE_INTERRUPT_TRANSFER */
	gboolean interface_autodetect;
	FuHidDeviceFlags flags;
} FuHidDevicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuHidDevice, fu_hid_device, FU_TYPE_USB_DEVICE)

enum { PROP_0, PROP_INTERFACE, PROP_LAST };

#define GET_PRIVATE(o) (fu_hid_device_get_instance_private(o))

static void
fu_hid_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuHidDevice *self = FU_HID_DEVICE(device);
	FuHidDevicePrivate *priv = GET_PRIVATE(self);
	fwupd_codec_string_append_bool(str, idt, "InterfaceAutodetect", priv->interface_autodetect);
	fwupd_codec_string_append_hex(str, idt, "Interface", priv->interface);
	fwupd_codec_string_append_hex(str, idt, "EpAddrIn", priv->ep_addr_in);
	fwupd_codec_string_append_hex(str, idt, "EpAddrOut", priv->ep_addr_out);
}

static void
fu_hid_device_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	FuHidDevice *device = FU_HID_DEVICE(object);
	FuHidDevicePrivate *priv = GET_PRIVATE(device);
	switch (prop_id) {
	case PROP_INTERFACE:
		g_value_set_uint(value, priv->interface);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fu_hid_device_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	FuHidDevice *device = FU_HID_DEVICE(object);
	switch (prop_id) {
	case PROP_INTERFACE:
		fu_hid_device_set_interface(device, g_value_get_uint(value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

/**
 * fu_hid_device_parse_descriptors:
 * @self: a #FuHidDevice
 * @error: (nullable): optional return location for an error
 *
 * Parses the HID descriptors.
 *
 * Returns: (transfer container) (element-type FuHidDescriptor): descriptors, or %NULL for error
 *
 * Since: 2.0.0
 **/
GPtrArray *
fu_hid_device_parse_descriptors(FuHidDevice *self, GError **error)
{
	g_autoptr(GPtrArray) fws = NULL;
	g_autoptr(GPtrArray) descriptors =
	    g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);

	g_return_val_if_fail(FU_HID_DEVICE(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	fws = fu_usb_device_get_hid_descriptors(FU_USB_DEVICE(self), error);
	if (fws == NULL)
		return NULL;
	for (guint i = 0; i < fws->len; i++) {
		GBytes *fw = g_ptr_array_index(fws, i);
		g_autoptr(FuFirmware) descriptor = fu_hid_descriptor_new();
		g_autofree gchar *title = g_strdup_printf("HidDescriptor:0x%x", i);
		fu_dump_bytes(G_LOG_DOMAIN, title, fw);
		if (!fu_firmware_parse_bytes(descriptor,
					     fw,
					     0x0,
					     FU_FIRMWARE_PARSE_FLAG_NONE,
					     error))
			return NULL;
		g_ptr_array_add(descriptors, g_steal_pointer(&descriptor));
	}
	return g_steal_pointer(&descriptors);
}

static gboolean
fu_hid_device_autodetect_eps(FuHidDevice *self, FuUsbInterface *iface, GError **error)
{
	FuHidDevicePrivate *priv = GET_PRIVATE(self);
	g_autoptr(GPtrArray) eps = fu_usb_interface_get_endpoints(iface);
	for (guint i = 0; eps != NULL && i < eps->len; i++) {
		FuUsbEndpoint *ep = g_ptr_array_index(eps, i);
		if (fu_usb_endpoint_get_direction(ep) == FU_USB_DIRECTION_DEVICE_TO_HOST &&
		    priv->ep_addr_in == 0) {
			priv->ep_addr_in = fu_usb_endpoint_get_address(ep);
			continue;
		}
		if (fu_usb_endpoint_get_direction(ep) == FU_USB_DIRECTION_HOST_TO_DEVICE &&
		    priv->ep_addr_out == 0) {
			priv->ep_addr_out = fu_usb_endpoint_get_address(ep);
			continue;
		}
	}
	if (priv->ep_addr_in == 0x0 && priv->ep_addr_out == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "could not autodetect EP addresses");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_hid_device_setup(FuDevice *device, GError **error)
{
	FuHidDevice *self = FU_HID_DEVICE(device);

	/* FuUsbDevice->setup */
	if (!FU_DEVICE_CLASS(fu_hid_device_parent_class)->setup(device, error))
		return FALSE;

	/* best effort, from HID */
	if (fu_device_get_vendor(device) == NULL) {
		g_autofree gchar *manufacturer =
		    fu_udev_device_read_sysfs(FU_UDEV_DEVICE(self),
					      "manufacturer",
					      FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
					      NULL);
		if (manufacturer != NULL)
			fu_device_set_vendor(device, manufacturer);
	}
	if (fu_device_get_name(device) == NULL) {
		g_autofree gchar *product =
		    fu_udev_device_read_sysfs(FU_UDEV_DEVICE(self),
					      "product",
					      FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
					      NULL);
		if (product != NULL)
			fu_device_set_name(device, product);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_hid_device_open(FuDevice *device, GError **error)
{
	FuHidDevice *self = FU_HID_DEVICE(device);
	FuHidDevicePrivate *priv = GET_PRIVATE(self);
	FuUsbDeviceClaimFlags flags = 0;

	/* FuUsbDevice->open */
	if (!FU_DEVICE_CLASS(fu_hid_device_parent_class)->open(device, error))
		return FALSE;

	/* self tests */
	if (fu_usb_device_get_spec(FU_USB_DEVICE(device)) == 0x0)
		return TRUE;

	/* auto-detect */
	if (priv->interface_autodetect) {
		g_autoptr(GPtrArray) ifaces = NULL;
		ifaces = fu_usb_device_get_interfaces(FU_USB_DEVICE(self), error);
		if (ifaces == NULL)
			return FALSE;
		for (guint i = 0; i < ifaces->len; i++) {
			FuUsbInterface *iface = g_ptr_array_index(ifaces, i);
			if (fu_usb_interface_get_class(iface) == FU_USB_CLASS_HID) {
				priv->interface = fu_usb_interface_get_number(iface);
				priv->interface_autodetect = FALSE;
				if (priv->flags & FU_HID_DEVICE_FLAG_AUTODETECT_EPS) {
					if (!fu_hid_device_autodetect_eps(self, iface, error))
						return FALSE;
				}
				break;
			}
		}
		if (priv->interface_autodetect) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "could not autodetect HID interface");
			return FALSE;
		}
	}

	/* claim */
	if ((priv->flags & FU_HID_DEVICE_FLAG_NO_KERNEL_UNBIND) == 0)
		flags |= FU_USB_DEVICE_CLAIM_FLAG_KERNEL_DRIVER;
	if (!fu_usb_device_claim_interface(FU_USB_DEVICE(self), priv->interface, flags, error)) {
		g_prefix_error(error, "failed to claim HID interface: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_hid_device_close(FuDevice *device, GError **error)
{
	FuHidDevice *self = FU_HID_DEVICE(device);
	FuHidDevicePrivate *priv = GET_PRIVATE(self);
	FuUsbDeviceClaimFlags flags = 0;
	g_autoptr(GError) error_local = NULL;

	/* self tests */
	if (fu_usb_device_get_spec(FU_USB_DEVICE(device)) == 0x0)
		return TRUE;

	/* release */
	if ((priv->flags & FU_HID_DEVICE_FLAG_NO_KERNEL_REBIND) == 0)
		flags |= FU_USB_DEVICE_CLAIM_FLAG_KERNEL_DRIVER;
	if (!fu_usb_device_release_interface(FU_USB_DEVICE(self),
					     priv->interface,
					     flags,
					     &error_local)) {
		if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND) ||
		    g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_INTERNAL)) {
			g_debug("ignoring: %s", error_local->message);
		} else {
			g_propagate_prefixed_error(error,
						   g_steal_pointer(&error_local),
						   "failed to release HID interface: ");
			return FALSE;
		}
	}

	/* FuUsbDevice->close */
	return FU_DEVICE_CLASS(fu_hid_device_parent_class)->close(device, error);
}

/**
 * fu_hid_device_set_interface:
 * @self: a #FuHidDevice
 * @interface_number: an interface number, e.g. `0x03`
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
fu_hid_device_set_interface(FuHidDevice *self, guint8 interface_number)
{
	FuHidDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_HID_DEVICE(self));
	priv->interface = interface_number;
	priv->interface_autodetect = FALSE;
}

/**
 * fu_hid_device_get_interface:
 * @self: a #FuHidDevice
 *
 * Gets the HID USB interface number.
 *
 * Returns: integer
 *
 * Since: 1.4.0
 **/
guint8
fu_hid_device_get_interface(FuHidDevice *self)
{
	FuHidDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_HID_DEVICE(self), 0xff);
	return priv->interface;
}

/**
 * fu_hid_device_set_ep_addr_in:
 * @self: a #FuHidDevice
 * @ep_addr_in: an endpoint, e.g. `0x03`
 *
 * Sets the HID USB interrupt in endpoint.
 *
 * In most cases the HID ep_addr_in is auto-detected, but this function can be
 * used where there are multiple HID EPss or where the device USB EP is invalid.
 *
 * Since: 1.9.4
 **/
void
fu_hid_device_set_ep_addr_in(FuHidDevice *self, guint8 ep_addr_in)
{
	FuHidDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_HID_DEVICE(self));
	priv->ep_addr_in = ep_addr_in;
	priv->interface_autodetect = FALSE;
}

/**
 * fu_hid_device_get_ep_addr_in:
 * @self: a #FuHidDevice
 *
 * Gets the HID USB in endpoint.
 *
 * Returns: integer
 *
 * Since: 1.9.4
 **/
guint8
fu_hid_device_get_ep_addr_in(FuHidDevice *self)
{
	FuHidDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_HID_DEVICE(self), 0xff);
	return priv->ep_addr_in;
}

/**
 * fu_hid_device_set_ep_addr_out:
 * @self: a #FuHidDevice
 * @ep_addr_out: an endpoint, e.g. `0x03`
 *
 * Sets the HID USB interrupt out endpoint.
 *
 * In most cases the HID EPs are auto-detected, but this function can be
 * used where there are multiple HID EPs or where the device USB EP is invalid.
 *
 * Since: 1.9.4
 **/
void
fu_hid_device_set_ep_addr_out(FuHidDevice *self, guint8 ep_addr_out)
{
	FuHidDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_HID_DEVICE(self));
	priv->ep_addr_out = ep_addr_out;
	priv->interface_autodetect = FALSE;
}

/**
 * fu_hid_device_get_ep_addr_out:
 * @self: a #FuHidDevice
 *
 * Gets the HID USB out endpoint.
 *
 * Returns: integer
 *
 * Since: 1.9.4
 **/
guint8
fu_hid_device_get_ep_addr_out(FuHidDevice *self)
{
	FuHidDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_HID_DEVICE(self), 0xff);
	return priv->ep_addr_out;
}

/**
 * fu_hid_device_add_flag:
 * @self: a #FuHidDevice
 * @flag: HID device flags, e.g. %FU_HID_DEVICE_FLAG_RETRY_FAILURE
 *
 * Adds a flag to be used for all set and get report messages.
 *
 * Since: 1.5.2
 **/
void
fu_hid_device_add_flag(FuHidDevice *self, FuHidDeviceFlags flag)
{
	FuHidDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_HID_DEVICE(self));
	priv->flags |= flag;
}

typedef struct {
	guint8 value;
	guint8 *buf;
	gsize bufsz;
	guint timeout;
	FuHidDeviceFlags flags;
} FuHidDeviceRetryHelper;

static gboolean
fu_hid_device_set_report_internal(FuHidDevice *self, FuHidDeviceRetryHelper *helper, GError **error)
{
	FuHidDevicePrivate *priv = GET_PRIVATE(self);
	gsize actual_len = 0;

	/* what method do we use? */
	if (helper->flags & FU_HID_DEVICE_FLAG_USE_INTERRUPT_TRANSFER) {
		g_autofree gchar *title =
		    g_strdup_printf("HID::SetReport [EP=0x%02x]", priv->ep_addr_out);
		fu_dump_raw(G_LOG_DOMAIN, title, helper->buf, helper->bufsz);
		if (priv->ep_addr_out == 0x0) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "no EpAddrOut set");
			return FALSE;
		}
		if (!fu_usb_device_interrupt_transfer(FU_USB_DEVICE(self),
						      priv->ep_addr_out,
						      helper->buf,
						      helper->bufsz,
						      &actual_len,
						      helper->timeout,
						      NULL, /* cancellable */
						      error)) {
			g_prefix_error(error, "failed to SetReport [interrupt-transfer]: ");
			return FALSE;
		}
	} else {
		guint16 wvalue = (FU_HID_REPORT_TYPE_OUTPUT << 8) | helper->value;
		g_autofree gchar *title = NULL;

		/* special case */
		if (helper->flags & FU_HID_DEVICE_FLAG_IS_FEATURE)
			wvalue = (FU_HID_REPORT_TYPE_FEATURE << 8) | helper->value;

		title = g_strdup_printf("HID::SetReport [wValue=0x%04x, wIndex=%u]",
					wvalue,
					priv->interface);
		fu_dump_raw(G_LOG_DOMAIN, title, helper->buf, helper->bufsz);
		if (!fu_usb_device_control_transfer(FU_USB_DEVICE(self),
						    FU_USB_DIRECTION_HOST_TO_DEVICE,
						    FU_USB_REQUEST_TYPE_CLASS,
						    FU_USB_RECIPIENT_INTERFACE,
						    FU_HID_REPORT_SET,
						    wvalue,
						    priv->interface,
						    helper->buf,
						    helper->bufsz,
						    &actual_len,
						    helper->timeout,
						    NULL,
						    error)) {
			g_prefix_error(error, "failed to SetReport: ");
			return FALSE;
		}
	}
	if ((helper->flags & FU_HID_DEVICE_FLAG_ALLOW_TRUNC) == 0 && actual_len != helper->bufsz) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "wrote %" G_GSIZE_FORMAT ", requested %" G_GSIZE_FORMAT " bytes",
			    actual_len,
			    helper->bufsz);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_hid_device_set_report_internal_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuHidDevice *self = FU_HID_DEVICE(device);
	FuHidDeviceRetryHelper *helper = (FuHidDeviceRetryHelper *)user_data;
	return fu_hid_device_set_report_internal(self, helper, error);
}

/**
 * fu_hid_device_set_report:
 * @self: a #FuHidDevice
 * @value: low byte of wValue, but unused when using %FU_HID_DEVICE_FLAG_USE_INTERRUPT_TRANSFER
 * @buf: (nullable): a mutable buffer of data to send
 * @bufsz: size of @buf
 * @timeout: timeout in ms
 * @flags: HID device flags e.g. %FU_HID_DEVICE_FLAG_ALLOW_TRUNC
 * @error: (nullable): optional return location for an error
 *
 * Calls SetReport on the hardware.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.4.0
 **/
gboolean
fu_hid_device_set_report(FuHidDevice *self,
			 guint8 value,
			 guint8 *buf,
			 gsize bufsz,
			 guint timeout,
			 FuHidDeviceFlags flags,
			 GError **error)
{
	FuHidDeviceRetryHelper helper;
	FuHidDevicePrivate *priv = GET_PRIVATE(self);

	g_return_val_if_fail(FU_HID_DEVICE(self), FALSE);
	g_return_val_if_fail(buf != NULL, FALSE);
	g_return_val_if_fail(bufsz != 0, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* create helper */
	helper.value = value;
	helper.buf = buf;
	helper.bufsz = bufsz;
	helper.timeout = timeout;
	helper.flags = priv->flags | flags;

	/* special case */
	if (flags & FU_HID_DEVICE_FLAG_RETRY_FAILURE) {
		return fu_device_retry(FU_DEVICE(self),
				       fu_hid_device_set_report_internal_cb,
				       FU_HID_DEVICE_RETRIES,
				       &helper,
				       error);
	}

	/* just one */
	return fu_hid_device_set_report_internal(self, &helper, error);
}

static gboolean
fu_hid_device_get_report_internal(FuHidDevice *self, FuHidDeviceRetryHelper *helper, GError **error)
{
	FuHidDevicePrivate *priv = GET_PRIVATE(self);
	gsize actual_len = 0;

	/* what method do we use? */
	if (helper->flags & FU_HID_DEVICE_FLAG_USE_INTERRUPT_TRANSFER) {
		g_autofree gchar *title = NULL;
		if (priv->ep_addr_in == 0x0) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "no EpAddrIn set");
			return FALSE;
		}
		if (!fu_usb_device_interrupt_transfer(FU_USB_DEVICE(self),
						      priv->ep_addr_in,
						      helper->buf,
						      helper->bufsz,
						      &actual_len,
						      helper->timeout,
						      NULL, /* cancellable */
						      error))
			return FALSE;
		title = g_strdup_printf("HID::GetReport [EP=0x%02x]", priv->ep_addr_in);
		fu_dump_raw(G_LOG_DOMAIN, title, helper->buf, helper->bufsz);
	} else {
		guint16 wvalue = (FU_HID_REPORT_TYPE_INPUT << 8) | helper->value;
		g_autofree gchar *title = NULL;

		/* special case */
		if (helper->flags & FU_HID_DEVICE_FLAG_IS_FEATURE)
			wvalue = (FU_HID_REPORT_TYPE_FEATURE << 8) | helper->value;

		title = g_strdup_printf("HID::GetReport [wValue=0x%04x, wIndex=%u]",
					wvalue,
					priv->interface);
		fu_dump_raw(G_LOG_DOMAIN, title, helper->buf, helper->bufsz);
		if (!fu_usb_device_control_transfer(FU_USB_DEVICE(self),
						    FU_USB_DIRECTION_DEVICE_TO_HOST,
						    FU_USB_REQUEST_TYPE_CLASS,
						    FU_USB_RECIPIENT_INTERFACE,
						    FU_HID_REPORT_GET,
						    wvalue,
						    priv->interface,
						    helper->buf,
						    helper->bufsz,
						    &actual_len, /* actual length */
						    helper->timeout,
						    NULL,
						    error)) {
			g_prefix_error(error, "failed to GetReport: ");
			return FALSE;
		}
		fu_dump_raw(G_LOG_DOMAIN, title, helper->buf, actual_len);
	}
	if ((helper->flags & FU_HID_DEVICE_FLAG_ALLOW_TRUNC) == 0 && actual_len != helper->bufsz) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "read %" G_GSIZE_FORMAT ", requested %" G_GSIZE_FORMAT " bytes",
			    actual_len,
			    helper->bufsz);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_hid_device_get_report_internal_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuHidDevice *self = FU_HID_DEVICE(device);
	FuHidDeviceRetryHelper *helper = (FuHidDeviceRetryHelper *)user_data;
	return fu_hid_device_get_report_internal(self, helper, error);
}

/**
 * fu_hid_device_get_report:
 * @self: a #FuHidDevice
 * @value: low byte of wValue, but unused when using %FU_HID_DEVICE_FLAG_USE_INTERRUPT_TRANSFER
 * @buf: (nullable): a mutable buffer of data to send
 * @bufsz: size of @buf
 * @timeout: timeout in ms
 * @flags: HID device flags e.g. %FU_HID_DEVICE_FLAG_ALLOW_TRUNC
 * @error: (nullable): optional return location for an error
 *
 * Calls GetReport on the hardware.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.4.0
 **/
gboolean
fu_hid_device_get_report(FuHidDevice *self,
			 guint8 value,
			 guint8 *buf,
			 gsize bufsz,
			 guint timeout,
			 FuHidDeviceFlags flags,
			 GError **error)
{
	FuHidDeviceRetryHelper helper;
	FuHidDevicePrivate *priv = GET_PRIVATE(self);

	g_return_val_if_fail(FU_HID_DEVICE(self), FALSE);
	g_return_val_if_fail(buf != NULL, FALSE);
	g_return_val_if_fail(bufsz != 0, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* create helper */
	helper.value = value;
	helper.buf = buf;
	helper.bufsz = bufsz;
	helper.timeout = timeout;
	helper.flags = priv->flags | flags;

	/* special case */
	if (flags & FU_HID_DEVICE_FLAG_RETRY_FAILURE) {
		return fu_device_retry(FU_DEVICE(self),
				       fu_hid_device_get_report_internal_cb,
				       FU_HID_DEVICE_RETRIES,
				       &helper,
				       error);
	}

	/* just one */
	return fu_hid_device_get_report_internal(self, &helper, error);
}

static void
fu_hid_device_init(FuHidDevice *self)
{
	FuHidDevicePrivate *priv = GET_PRIVATE(self);
	priv->interface_autodetect = TRUE;
}

static void
fu_hid_device_class_init(FuHidDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	GParamSpec *pspec;

	object_class->get_property = fu_hid_device_get_property;
	object_class->set_property = fu_hid_device_set_property;
	device_class->open = fu_hid_device_open;
	device_class->setup = fu_hid_device_setup;
	device_class->close = fu_hid_device_close;
	device_class->to_string = fu_hid_device_to_string;

	/**
	 * FuHidDevice:interface:
	 *
	 * The HID interface to use.
	 *
	 * Since: 1.4.0
	 */
	pspec = g_param_spec_uint("interface",
				  NULL,
				  NULL,
				  0x00,
				  0xff,
				  0x00,
				  G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_INTERFACE, pspec);
}
