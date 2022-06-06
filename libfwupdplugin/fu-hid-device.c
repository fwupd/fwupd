/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuHidDevice"

#include "config.h"

#include "fu-dump.h"
#include "fu-hid-device.h"
#include "fu-string.h"

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
	fu_string_append_kb(str, idt, "InterfaceAutodetect", priv->interface_autodetect);
	fu_string_append_kx(str, idt, "Interface", priv->interface);
	if (priv->ep_addr_in != 0)
		fu_string_append_kx(str, idt, "EpAddrIn", priv->ep_addr_in);
	if (priv->ep_addr_out != 0)
		fu_string_append_kx(str, idt, "EpAddrOut", priv->ep_addr_out);
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

#ifdef HAVE_GUSB
static gboolean
fu_hid_device_autodetect_eps(FuHidDevice *self, GUsbInterface *iface, GError **error)
{
#if G_USB_CHECK_VERSION(0, 3, 3)
	FuHidDevicePrivate *priv = GET_PRIVATE(self);
	g_autoptr(GPtrArray) eps = g_usb_interface_get_endpoints(iface);
	for (guint i = 0; i < eps->len; i++) {
		GUsbEndpoint *ep = g_ptr_array_index(eps, i);
		if (g_usb_endpoint_get_direction(ep) == G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST &&
		    priv->ep_addr_in == 0) {
			priv->ep_addr_in = g_usb_endpoint_get_address(ep);
			continue;
		}
		if (g_usb_endpoint_get_direction(ep) == G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE &&
		    priv->ep_addr_out == 0) {
			priv->ep_addr_out = g_usb_endpoint_get_address(ep);
			continue;
		}
	}
	if (priv->ep_addr_in == 0x0 || priv->ep_addr_out == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "could not autodetect EP addresses");
		return FALSE;
	}
	return TRUE;
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "this version of GUsb is not supported");
	return FALSE;
#endif
}
#endif

static gboolean
fu_hid_device_open(FuDevice *device, GError **error)
{
#ifdef HAVE_GUSB
	FuHidDevice *self = FU_HID_DEVICE(device);
	FuHidDevicePrivate *priv = GET_PRIVATE(self);
	GUsbDeviceClaimInterfaceFlags flags = 0;
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(device));

	/* FuUsbDevice->open */
	if (!FU_DEVICE_CLASS(fu_hid_device_parent_class)->open(device, error))
		return FALSE;

	/* auto-detect */
	if (priv->interface_autodetect) {
		g_autoptr(GPtrArray) ifaces = NULL;
		ifaces = g_usb_device_get_interfaces(usb_device, error);
		if (ifaces == NULL)
			return FALSE;
		for (guint i = 0; i < ifaces->len; i++) {
			GUsbInterface *iface = g_ptr_array_index(ifaces, i);
			if (g_usb_interface_get_class(iface) == G_USB_DEVICE_CLASS_HID) {
				priv->interface = g_usb_interface_get_number(iface);
				priv->interface_autodetect = FALSE;
				if (priv->flags & FU_HID_DEVICE_FLAG_USE_INTERRUPT_TRANSFER) {
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
		flags |= G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER;
	if (!g_usb_device_claim_interface(usb_device, priv->interface, flags, error)) {
		g_prefix_error(error, "failed to claim HID interface: ");
		return FALSE;
	}
#endif

	/* success */
	return TRUE;
}

static gboolean
fu_hid_device_close(FuDevice *device, GError **error)
{
#ifdef HAVE_GUSB
	FuHidDevice *self = FU_HID_DEVICE(device);
	FuHidDevicePrivate *priv = GET_PRIVATE(self);
	GUsbDeviceClaimInterfaceFlags flags = 0;
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(device));
	g_autoptr(GError) error_local = NULL;
#endif

#ifdef HAVE_GUSB
	/* release */
	if ((priv->flags & FU_HID_DEVICE_FLAG_NO_KERNEL_REBIND) == 0)
		flags |= G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER;
	if (!g_usb_device_release_interface(usb_device, priv->interface, flags, &error_local)) {
		if (g_error_matches(error_local,
				    G_USB_DEVICE_ERROR,
				    G_USB_DEVICE_ERROR_NO_DEVICE) ||
		    g_error_matches(error_local, G_USB_DEVICE_ERROR, G_USB_DEVICE_ERROR_INTERNAL)) {
			g_debug("ignoring: %s", error_local->message);
			return TRUE;
		}
		g_propagate_prefixed_error(error,
					   g_steal_pointer(&error_local),
					   "failed to release HID interface: ");
		return FALSE;
	}
#endif

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
#ifdef HAVE_GUSB
	FuHidDevicePrivate *priv = GET_PRIVATE(self);
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(self));
	gsize actual_len = 0;

	/* what method do we use? */
	if (priv->flags & FU_HID_DEVICE_FLAG_USE_INTERRUPT_TRANSFER) {
		if (g_getenv("FU_HID_DEVICE_VERBOSE") != NULL) {
			g_autofree gchar *title = NULL;
			title = g_strdup_printf("HID::SetReport [EP=0x%02x]", priv->ep_addr_out);
			fu_dump_raw(G_LOG_DOMAIN, title, helper->buf, helper->bufsz);
		}
		if (!g_usb_device_interrupt_transfer(usb_device,
						     priv->ep_addr_out,
						     helper->buf,
						     helper->bufsz,
						     &actual_len,
						     helper->timeout,
						     NULL, /* cancellable */
						     error)) {
			return FALSE;
		}
	} else {
		guint16 wvalue = (FU_HID_REPORT_TYPE_OUTPUT << 8) | helper->value;

		/* special case */
		if (helper->flags & FU_HID_DEVICE_FLAG_IS_FEATURE)
			wvalue = (FU_HID_REPORT_TYPE_FEATURE << 8) | helper->value;

		if (g_getenv("FU_HID_DEVICE_VERBOSE") != NULL) {
			g_autofree gchar *title = NULL;
			title = g_strdup_printf("HID::SetReport [wValue=0x%04x ,wIndex=%u]",
						wvalue,
						priv->interface);
			fu_dump_raw(G_LOG_DOMAIN, title, helper->buf, helper->bufsz);
		}
		if (!g_usb_device_control_transfer(usb_device,
						   G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
						   G_USB_DEVICE_REQUEST_TYPE_CLASS,
						   G_USB_DEVICE_RECIPIENT_INTERFACE,
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
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "wrote %" G_GSIZE_FORMAT ", requested %" G_GSIZE_FORMAT " bytes",
			    actual_len,
			    helper->bufsz);
		return FALSE;
	}
#endif
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
#ifdef HAVE_GUSB
	FuHidDevicePrivate *priv = GET_PRIVATE(self);
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(self));
	gsize actual_len = 0;

	/* what method do we use? */
	if (priv->flags & FU_HID_DEVICE_FLAG_USE_INTERRUPT_TRANSFER) {
		if (!g_usb_device_interrupt_transfer(usb_device,
						     priv->ep_addr_in,
						     helper->buf,
						     helper->bufsz,
						     &actual_len,
						     helper->timeout,
						     NULL, /* cancellable */
						     error)) {
			return FALSE;
		}
		if (g_getenv("FU_HID_DEVICE_VERBOSE") != NULL) {
			g_autofree gchar *title = NULL;
			title = g_strdup_printf("HID::GetReport [EP=0x%02x]", priv->ep_addr_in);
			fu_dump_raw(G_LOG_DOMAIN, title, helper->buf, helper->bufsz);
		}
	} else {
		guint16 wvalue = (FU_HID_REPORT_TYPE_INPUT << 8) | helper->value;

		/* special case */
		if (helper->flags & FU_HID_DEVICE_FLAG_IS_FEATURE)
			wvalue = (FU_HID_REPORT_TYPE_FEATURE << 8) | helper->value;

		if (g_getenv("FU_HID_DEVICE_VERBOSE") != NULL) {
			g_autofree gchar *title = NULL;
			title = g_strdup_printf("HID::GetReport [wValue=0x%04x, wIndex=%u]",
						wvalue,
						priv->interface);
			fu_dump_raw(G_LOG_DOMAIN, title, helper->buf, actual_len);
		}
		if (!g_usb_device_control_transfer(usb_device,
						   G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
						   G_USB_DEVICE_REQUEST_TYPE_CLASS,
						   G_USB_DEVICE_RECIPIENT_INTERFACE,
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
		if (g_getenv("FU_HID_DEVICE_VERBOSE") != NULL) {
			g_autofree gchar *title = NULL;
			title = g_strdup_printf("HID::GetReport [wValue=0x%04x, wIndex=%u]",
						wvalue,
						priv->interface);
			fu_dump_raw(G_LOG_DOMAIN, title, helper->buf, actual_len);
		}
	}
	if ((helper->flags & FU_HID_DEVICE_FLAG_ALLOW_TRUNC) == 0 && actual_len != helper->bufsz) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "read %" G_GSIZE_FORMAT ", requested %" G_GSIZE_FORMAT " bytes",
			    actual_len,
			    helper->bufsz);
		return FALSE;
	}
#endif
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

/**
 * fu_hid_device_new:
 * @usb_device: a USB device
 *
 * Creates a new HID device.
 *
 * Returns: (transfer full): a #FuHidDevice
 *
 * Since: 1.4.0
 **/
FuHidDevice *
fu_hid_device_new(GUsbDevice *usb_device)
{
	FuHidDevice *device = g_object_new(FU_TYPE_HID_DEVICE, NULL);
	fu_usb_device_set_dev(FU_USB_DEVICE(device), usb_device);
	return FU_HID_DEVICE(device);
}

static void
fu_hid_device_class_init(FuHidDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	GParamSpec *pspec;

	object_class->get_property = fu_hid_device_get_property;
	object_class->set_property = fu_hid_device_set_property;
	klass_device->open = fu_hid_device_open;
	klass_device->close = fu_hid_device_close;
	klass_device->to_string = fu_hid_device_to_string;

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
