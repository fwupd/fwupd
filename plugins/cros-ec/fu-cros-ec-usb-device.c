/*
 * Copyright (C) 2020 Benson Leung <bleung@chromium.org>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fu-cros-ec-usb-device.h"
#include "fu-cros-ec-common.h"

#define USB_SUBCLASS_GOOGLE_UPDATE	0x53
#define USB_PROTOCOL_GOOGLE_UPDATE	0xff

#define SETUP_RETRY_CNT			5
#define FLUSH_TIMEOUT_MS		10
#define BULK_SEND_TIMEOUT_MS		2000
#define BULK_RECV_TIMEOUT_MS		5000

struct _FuCrosEcUsbDevice {
	FuUsbDevice		parent_instance;
	guint8			iface_idx; 	/* bInterfaceNumber */
	guint8			ep_num;		/* bEndpointAddress */
	guint16			chunk_len; 	/* wMaxPacketSize */

	struct			first_response_pdu targ;
	guint16			protocol_version;
	guint16			header_type;
};

G_DEFINE_TYPE (FuCrosEcUsbDevice, fu_cros_ec_usb_device, FU_TYPE_USB_DEVICE)

typedef union _START_RESP {
	struct first_response_pdu rpdu;
	guint32 legacy_resp;
} START_RESP;

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
fu_cros_ec_usb_device_do_xfer (FuCrosEcUsbDevice * self, guint8 *outbuf,
			       gsize outlen, guint8 *inbuf, gsize inlen,
			       gboolean allow_less, gsize *rxed_count,
			       GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));
	gsize actual = 0;

	/* send data out */
	if (outbuf != NULL && outlen > 0) {
		if (!g_usb_device_bulk_transfer (usb_device, self->ep_num,
						 outbuf, outlen,
						 &actual, BULK_SEND_TIMEOUT_MS,
						 NULL, error)) {
			return FALSE;
		}
		if (actual != outlen) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_PARTIAL_INPUT,
				     "only sent %" G_GSIZE_FORMAT "/%"
				     G_GSIZE_FORMAT " bytes",
				     actual, outlen);
			return FALSE;
		}
	}

	/* read reply back */
	if (inbuf != NULL && inlen > 0) {
		actual = 0;
		if (!g_usb_device_bulk_transfer (usb_device,
						 self->ep_num | 0x80,
						 inbuf, inlen,
						 &actual, BULK_RECV_TIMEOUT_MS,
						 NULL, error)) {
			return FALSE;
		}
		if (actual != inlen && !allow_less) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_PARTIAL_INPUT,
				     "only received %" G_GSIZE_FORMAT "/%"
				     G_GSIZE_FORMAT " bytes",
				     actual, outlen);
			return FALSE;
		}
	}

	if (rxed_count != NULL)
		*rxed_count = actual;

	return TRUE;
}

static gboolean
fu_cros_ec_usb_device_flush (FuDevice *device, gpointer user_data,
			     GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (device));
	FuCrosEcUsbDevice *self = FU_CROS_EC_USB_DEVICE (device);
	gsize actual = 0;
	g_autofree guint8 *inbuf = g_malloc0 (self->chunk_len);

	if (g_usb_device_bulk_transfer (usb_device, self->ep_num | 0x80, inbuf,
					self->chunk_len, &actual,
					FLUSH_TIMEOUT_MS, NULL, NULL)) {
		g_debug ("flushing %" G_GSIZE_FORMAT " bytes", actual);
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "flushing %" G_GSIZE_FORMAT " bytes", actual);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_cros_ec_usb_device_start_request (FuDevice *device, gpointer user_data,
				     GError **error)
{
	FuCrosEcUsbDevice *self = FU_CROS_EC_USB_DEVICE (device);
	guint8 *start_resp = (guint8 *) user_data;
	struct update_frame_header ufh;
	gsize rxed_size = 0;

	memset(&ufh, 0, sizeof (ufh));
	ufh.block_size = GUINT32_TO_BE (sizeof(ufh));
	if (!fu_cros_ec_usb_device_do_xfer (self, (guint8 *)&ufh, sizeof(ufh),
					    start_resp,
					    sizeof(START_RESP), TRUE,
					    &rxed_size, error))
		return FALSE;

	/* we got something, so check for errors in response */
	if (rxed_size < 8) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_PARTIAL_INPUT,
			     "unexpected response size %" G_GSIZE_FORMAT,
			     rxed_size);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_cros_ec_usb_device_setup (FuDevice *device, GError **error)
{
	FuCrosEcUsbDevice *self = FU_CROS_EC_USB_DEVICE (device);
	guint32 error_code;
	START_RESP start_resp;

	/* flush all data from endpoint to recover in case of error */
	if (!fu_device_retry (device, fu_cros_ec_usb_device_flush,
			      SETUP_RETRY_CNT, NULL, error)) {
		g_prefix_error (error, "failed to flush device to idle state: ");
		return FALSE;
	}

	/* send start request */
	if (!fu_device_retry (device, fu_cros_ec_usb_device_start_request,
			      SETUP_RETRY_CNT, &start_resp, error)) {
		g_prefix_error (error, "failed to send start request: ");
		return FALSE;
	}

	self->protocol_version = GUINT16_FROM_BE (start_resp.rpdu.protocol_version);

	if (self->protocol_version < 5 || self->protocol_version > 6) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_SUPPORTED,
			     "unsupported protocol version %d",
			     self->protocol_version);
		return FALSE;
	}
	self->header_type = GUINT16_FROM_BE (start_resp.rpdu.header_type);

	error_code = GUINT32_FROM_BE (start_resp.rpdu.return_value);
	if (error_code != 0) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "target reporting error %u", error_code);
		return FALSE;
	}

	memcpy (self->targ.common.version, start_resp.rpdu.common.version,
		sizeof(start_resp.rpdu.common.version));
	self->targ.common.maximum_pdu_size =
		GUINT32_FROM_BE (start_resp.rpdu.common.maximum_pdu_size);
	self->targ.common.flash_protection =
		GUINT32_FROM_BE (start_resp.rpdu.common.flash_protection);
	self->targ.common.min_rollback = GINT32_FROM_BE (start_resp.rpdu.common.min_rollback);
	self->targ.common.key_version = GUINT32_FROM_BE (start_resp.rpdu.common.key_version);

	fu_device_set_version (FU_DEVICE (device), self->targ.common.version);

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
	fu_device_set_version_format (FU_DEVICE (device), FWUPD_VERSION_FORMAT_PLAIN);
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
