/*
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-steelseries-device.h"

typedef struct {
	gint iface_idx_offset;
	guint8 iface_idx;
	guint8 ep;
	gsize ep_in_size;
} FuSteelseriesDevicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuSteelseriesDevice, fu_steelseries_device, FU_TYPE_USB_DEVICE)
#define GET_PRIVATE(o) (fu_steelseries_device_get_instance_private(o))

gsize
fu_steelseries_device_get_transfer_size(FuSteelseriesDevice *self)
{
	FuSteelseriesDevicePrivate *priv = GET_PRIVATE(self);
	return priv->ep_in_size;
}

/* @iface_idx_offset can be negative to specify from the end */
void
fu_steelseries_device_set_iface_idx_offset(FuSteelseriesDevice *self, gint iface_idx_offset)
{
	FuSteelseriesDevicePrivate *priv = GET_PRIVATE(self);
	priv->iface_idx_offset = iface_idx_offset;
}

gboolean
fu_steelseries_device_cmd(FuSteelseriesDevice *self, guint8 *data, gboolean answer, GError **error)
{
	FuSteelseriesDevicePrivate *priv = GET_PRIVATE(self);
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(self));
	gsize actual_len = 0;
	gboolean ret;

	ret = g_usb_device_control_transfer(usb_device,
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_CLASS,
					    G_USB_DEVICE_RECIPIENT_INTERFACE,
					    0x09,
					    0x0200,
					    priv->iface_idx,
					    data,
					    STEELSERIES_BUFFER_CONTROL_SIZE,
					    &actual_len,
					    STEELSERIES_TRANSACTION_TIMEOUT,
					    NULL,
					    error);
	if (!ret) {
		g_prefix_error(error, "failed to do control transfer: ");
		return FALSE;
	}
	if (actual_len != STEELSERIES_BUFFER_CONTROL_SIZE) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "only wrote %" G_GSIZE_FORMAT "bytes",
			    actual_len);
		return FALSE;
	}

	/* cleanup the buffer before receiving any data */
	memset(data, 0x00, STEELSERIES_BUFFER_CONTROL_SIZE);

	/* do not expect the answer from device */
	if (answer != TRUE)
		return TRUE;

	ret = g_usb_device_interrupt_transfer(usb_device,
					      priv->ep,
					      data,
					      priv->ep_in_size,
					      &actual_len,
					      STEELSERIES_TRANSACTION_TIMEOUT,
					      NULL,
					      error);
	if (!ret) {
		g_prefix_error(error, "failed to do EP transfer: ");
		return FALSE;
	}
	if (actual_len != priv->ep_in_size) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "only read %" G_GSIZE_FORMAT "bytes",
			    actual_len);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_steelseries_device_probe(FuDevice *device, GError **error)
{
#if G_USB_CHECK_VERSION(0, 3, 3)
	FuSteelseriesDevice *self = FU_STEELSERIES_DEVICE(device);
	FuSteelseriesDevicePrivate *priv = GET_PRIVATE(self);
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(device));
	GUsbInterface *iface = NULL;
	GUsbEndpoint *ep = NULL;
	guint8 ep_id;
	guint16 packet_size;
	g_autoptr(GPtrArray) ifaces = NULL;
	g_autoptr(GPtrArray) endpoints = NULL;

	/* FuUsbDevice->probe */
	if (!FU_DEVICE_CLASS(fu_steelseries_device_parent_class)->probe(device, error))
		return FALSE;

	ifaces = g_usb_device_get_interfaces(usb_device, error);
	if (ifaces == NULL)
		return FALSE;

	/* use the correct interface for interrupt transfer, either specifying an absolute offset,
	 * or a negative offset value for the "last" one */
	if (priv->iface_idx_offset >= 0) {
		if ((guint)priv->iface_idx_offset > ifaces->len) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "update interface 0x%x not found",
				    (guint)priv->iface_idx_offset);
			return FALSE;
		}
		priv->iface_idx = priv->iface_idx_offset;
	} else {
		priv->iface_idx = ifaces->len - 1;
	}
	iface = g_ptr_array_index(ifaces, priv->iface_idx);

	endpoints = g_usb_interface_get_endpoints(iface);
	/* expecting to have only one endpoint for communication */
	if (endpoints == NULL || endpoints->len != 1) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "endpoint not found");
		return FALSE;
	}

	ep = g_ptr_array_index(endpoints, 0);
	ep_id = g_usb_endpoint_get_address(ep);
	packet_size = g_usb_endpoint_get_maximum_packet_size(ep);

	priv->ep = ep_id;
	priv->ep_in_size = packet_size;

	fu_usb_device_add_interface(FU_USB_DEVICE(self), priv->iface_idx);

	/* success */
	return TRUE;
#else
	/* failed */
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "this version of GUsb is not supported");
	return FALSE;
#endif
}

static void
fu_steelseries_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuSteelseriesDevice *self = FU_STEELSERIES_DEVICE(device);
	FuSteelseriesDevicePrivate *priv = GET_PRIVATE(self);

	FU_DEVICE_CLASS(fu_steelseries_device_parent_class)->to_string(device, idt, str);

	fu_common_string_append_kx(str, idt, "Interface", priv->iface_idx);
	fu_common_string_append_kx(str, idt, "Endpoint", priv->ep);
}

static void
fu_steelseries_device_init(FuSteelseriesDevice *self)
{
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_STEELSERIES_DEVICE_FLAG_IS_RECEIVER,
					"is-receiver");
}

static void
fu_steelseries_device_class_init(FuSteelseriesDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->to_string = fu_steelseries_device_to_string;
	klass_device->probe = fu_steelseries_device_probe;
}
