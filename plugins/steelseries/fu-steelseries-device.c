/*
 * Copyright 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
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

/* @iface_idx_offset can be negative to specify from the end */
void
fu_steelseries_device_set_iface_idx_offset(FuSteelseriesDevice *self, gint iface_idx_offset)
{
	FuSteelseriesDevicePrivate *priv = GET_PRIVATE(self);
	priv->iface_idx_offset = iface_idx_offset;
}

gboolean
fu_steelseries_device_cmd(FuSteelseriesDevice *self,
			  guint8 *data,
			  gsize datasz,
			  gboolean answer,
			  GError **error)
{
	FuSteelseriesDevicePrivate *priv = GET_PRIVATE(self);
	gsize actual_len = 0;

	if (!fu_usb_device_control_transfer(FU_USB_DEVICE(self),
					    FU_USB_DIRECTION_HOST_TO_DEVICE,
					    FU_USB_REQUEST_TYPE_CLASS,
					    FU_USB_RECIPIENT_INTERFACE,
					    0x09,
					    0x0200,
					    priv->iface_idx,
					    data,
					    datasz,
					    &actual_len,
					    STEELSERIES_TRANSACTION_TIMEOUT,
					    NULL,
					    error)) {
		g_prefix_error(error, "failed to do control transfer: ");
		return FALSE;
	}
	if (actual_len != datasz) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "only wrote %" G_GSIZE_FORMAT "bytes",
			    actual_len);
		return FALSE;
	}

	/* cleanup the buffer before receiving any data */
	memset(data, 0x00, datasz);

	/* do not expect the answer from device */
	if (answer != TRUE)
		return TRUE;

	if (!fu_usb_device_interrupt_transfer(FU_USB_DEVICE(self),
					      priv->ep,
					      data,
					      priv->ep_in_size,
					      &actual_len,
					      STEELSERIES_TRANSACTION_TIMEOUT,
					      NULL,
					      error)) {
		g_prefix_error(error, "failed to do EP transfer: ");
		fu_error_convert(error);
		return FALSE;
	}
	if (actual_len != priv->ep_in_size) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
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
	FuSteelseriesDevice *self = FU_STEELSERIES_DEVICE(device);
	FuSteelseriesDevicePrivate *priv = GET_PRIVATE(self);
	FuUsbInterface *iface = NULL;
	FuUsbEndpoint *ep = NULL;
	guint8 iface_idx;
	guint8 ep_id;
	guint16 packet_size;
	g_autoptr(GPtrArray) ifaces = NULL;
	g_autoptr(GPtrArray) endpoints = NULL;

	ifaces = fu_usb_device_get_interfaces(FU_USB_DEVICE(device), error);
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
		iface_idx = priv->iface_idx_offset;
	} else {
		iface_idx = ifaces->len - 1;
	}
	iface = g_ptr_array_index(ifaces, iface_idx);
	priv->iface_idx = fu_usb_interface_get_number(iface);

	endpoints = fu_usb_interface_get_endpoints(iface);
	/* expecting to have only one endpoint for communication */
	if (endpoints == NULL || endpoints->len != 1) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "endpoint not found");
		return FALSE;
	}

	ep = g_ptr_array_index(endpoints, 0);
	ep_id = fu_usb_endpoint_get_address(ep);
	packet_size = fu_usb_endpoint_get_maximum_packet_size(ep);

	priv->ep = ep_id;
	priv->ep_in_size = packet_size;

	fu_usb_device_add_interface(FU_USB_DEVICE(self), priv->iface_idx);

	/* success */
	return TRUE;
}

static void
fu_steelseries_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuSteelseriesDevice *self = FU_STEELSERIES_DEVICE(device);
	FuSteelseriesDevicePrivate *priv = GET_PRIVATE(self);
	fwupd_codec_string_append_hex(str, idt, "Interface", priv->iface_idx);
	fwupd_codec_string_append_hex(str, idt, "Endpoint", priv->ep);
}

static void
fu_steelseries_device_init(FuSteelseriesDevice *self)
{
	fu_device_register_private_flag(FU_DEVICE(self), FU_STEELSERIES_DEVICE_FLAG_IS_RECEIVER);
}

static void
fu_steelseries_device_class_init(FuSteelseriesDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->to_string = fu_steelseries_device_to_string;
	device_class->probe = fu_steelseries_device_probe;
}
