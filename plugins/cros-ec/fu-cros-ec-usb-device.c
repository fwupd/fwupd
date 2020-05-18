/*
 * Copyright (C) 2020 Benson Leung <bleung@chromium.org>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fu-cros-ec-usb-device.h"

#define USB_SUBCLASS_GOOGLE_UPDATE	0x53
#define USB_PROTOCOL_GOOGLE_UPDATE	0xff

struct _FuCrosEcUsbDevice {
	FuUsbDevice		parent_instance;
	guint8			iface_idx; 	/* bInterfaceNumber */
	guint8			ep_num;		/* bEndpointAddress */
	guint16			chunk_len; 	/* wMaxPacketSize */

};

G_DEFINE_TYPE (FuCrosEcUsbDevice, fu_cros_ec_usb_device, FU_TYPE_USB_DEVICE)

static gboolean
fu_cros_ec_usb_device_find_interface (FuUsbDevice *device,
				      GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (device);
	FuCrosEcUsbDevice *self = FU_CROS_EC_USB_DEVICE (device);
	g_autoptr(GPtrArray) intfs = NULL;

	/* based on usb_updater2's find_interfacei() and find_endpoint() */

	intfs = g_usb_device_get_interfaces (usb_device, error);
	if (intfs == NULL)
		return FALSE;
	for (guint i = 0; i < intfs->len; i++) {
		GUsbInterface *intf = g_ptr_array_index (intfs, i);
		if (g_usb_interface_get_class (intf) == 255 &&
		    g_usb_interface_get_subclass (intf) == USB_SUBCLASS_GOOGLE_UPDATE &&
		    g_usb_interface_get_protocol (intf) == USB_PROTOCOL_GOOGLE_UPDATE) {
			GUsbEndpoint *ep;
			g_autoptr(GPtrArray) endpoints = NULL;

			endpoints = g_usb_interface_get_endpoints (intf);
			if (NULL == endpoints || 0 == endpoints->len)
				continue;
			ep = g_ptr_array_index (endpoints, 0);
			self->iface_idx = g_usb_interface_get_number (intf);
			self->ep_num = g_usb_endpoint_get_address (ep) & 0x7f;
			self->chunk_len = g_usb_endpoint_get_maximum_packet_size (ep);

			return TRUE;
		}
	}
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_FOUND,
			     "no update interface found");
	return FALSE;
}

static gboolean
fu_cros_ec_usb_device_open (FuUsbDevice *device, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (device);
	FuCrosEcUsbDevice *self = FU_CROS_EC_USB_DEVICE (device);

	if (!g_usb_device_claim_interface (usb_device, self->iface_idx,
					   G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
					   error)) {
		g_prefix_error (error, "failed to claim interface: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_cros_ec_usb_device_probe (FuUsbDevice *device, GError **error)
{
	FuCrosEcUsbDevice *self = FU_CROS_EC_USB_DEVICE (device);

	/* very much like usb_updater2's usb_findit() */

	if (!fu_cros_ec_usb_device_find_interface (device, error)) {
		g_prefix_error (error, "failed to find update interface: ");
		return FALSE;
	}

	if (self->chunk_len == 0) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "wMaxPacketSize isn't valid: %" G_GUINT16_FORMAT,
			     self->chunk_len);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_cros_ec_usb_device_setup (FuDevice *device, GError **error)
{
	/* success */
	return TRUE;
}

static gboolean
fu_cros_ec_usb_device_close (FuUsbDevice *device, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (device);
	FuCrosEcUsbDevice *self = FU_CROS_EC_USB_DEVICE (device);

	if (!g_usb_device_release_interface (usb_device, self->iface_idx,
					     G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
					     error)) {
		g_prefix_error (error, "failed to release interface: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_cros_ec_usb_device_init (FuCrosEcUsbDevice *device)
{
	fu_device_set_version_format (FU_DEVICE (device), FWUPD_VERSION_FORMAT_TRIPLET);
}

static void
fu_cros_ec_usb_device_class_init (FuCrosEcUsbDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	FuUsbDeviceClass *klass_usb_device = FU_USB_DEVICE_CLASS (klass);
	klass_device->setup = fu_cros_ec_usb_device_setup;
	klass_usb_device->open = fu_cros_ec_usb_device_open;
	klass_usb_device->probe = fu_cros_ec_usb_device_probe;
	klass_usb_device->close = fu_cros_ec_usb_device_close;
}
