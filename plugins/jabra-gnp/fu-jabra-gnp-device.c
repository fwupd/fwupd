/*
 * Copyright 2023 GN Audio
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-jabra-gnp-child-device.h"
#include "fu-jabra-gnp-common.h"
#include "fu-jabra-gnp-device.h"
#include "fu-jabra-gnp-firmware.h"
#include "fu-jabra-gnp-image.h"

struct _FuJabraGnpDevice {
	FuUsbDevice parent_instance;
	guint8 fwu_protocol;
	guint8 iface_hid;
	guint8 sequence_number;
	guint8 address;
	guint8 epin;
	guint dfu_pid;
};

G_DEFINE_TYPE(FuJabraGnpDevice, fu_jabra_gnp_device, FU_TYPE_HID_DEVICE)

guint8
fu_jabra_gnp_device_get_iface_hid(FuJabraGnpDevice *self)
{
	g_return_val_if_fail(FU_IS_JABRA_GNP_DEVICE(self), G_MAXUINT8);
	return self->iface_hid;
}

guint8
fu_jabra_gnp_device_get_epin(FuJabraGnpDevice *self)
{
	g_return_val_if_fail(FU_IS_JABRA_GNP_DEVICE(self), G_MAXUINT8);
	return self->epin;
}

static void
fu_jabra_gnp_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuJabraGnpDevice *self = FU_JABRA_GNP_DEVICE(device);
	fwupd_codec_string_append_hex(str, idt, "FwuProtocol", self->fwu_protocol);
	fwupd_codec_string_append_hex(str, idt, "IfaceHid", self->iface_hid);
	fwupd_codec_string_append_hex(str, idt, "SequenceNumber", self->sequence_number);
	fwupd_codec_string_append_hex(str, idt, "Address", self->address);
	fwupd_codec_string_append_hex(str, idt, "DfuPid", self->dfu_pid);
}

static guint8
_fu_usb_device_get_interface_for_class(FuUsbDevice *usb_device, guint8 intf_class, GError **error)
{
	g_autoptr(GPtrArray) intfs = NULL;
	intfs = fu_usb_device_get_interfaces(usb_device, error);
	if (intfs == NULL)
		return 0xFF;
	for (guint i = 0; i < intfs->len; i++) {
		FuUsbInterface *intf = g_ptr_array_index(intfs, i);
		if (fu_usb_interface_get_class(intf) == intf_class)
			return fu_usb_interface_get_number(intf);
	}
	return 0xFF;
}

static gboolean
fu_jabra_gnp_device_probe(FuDevice *device, GError **error)
{
	FuJabraGnpDevice *self = FU_JABRA_GNP_DEVICE(device);
	g_autoptr(GPtrArray) ifaces = NULL;

	/* already set by parent */
	if (self->address == FU_JABRA_GNP_ADDRESS_OTA_CHILD)
		return TRUE;

	ifaces = fu_usb_device_get_interfaces(FU_USB_DEVICE(self), error);
	if (ifaces == NULL) {
		g_prefix_error(error, "update interface not found: ");
		return FALSE;
	}

	for (guint i = 0; i < ifaces->len; i++) {
		FuUsbInterface *iface = g_ptr_array_index(ifaces, i);
		if (fu_usb_interface_get_class(iface) == FU_USB_CLASS_HID) {
			FuUsbEndpoint *ep1;
			g_autoptr(GPtrArray) endpoints = fu_usb_interface_get_endpoints(iface);

			if (endpoints == NULL || endpoints->len < 1)
				continue;
			ep1 = g_ptr_array_index(endpoints, 0);
			self->epin = fu_usb_endpoint_get_address(ep1);
		}
	}
	if (self->epin == 0x0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "update endpoints not found");
		return FALSE;
	}

	self->iface_hid =
	    _fu_usb_device_get_interface_for_class(FU_USB_DEVICE(self), FU_USB_CLASS_HID, error);
	if (self->iface_hid == 0xFF) {
		g_prefix_error(error, "cannot find HID interface: ");
		return FALSE;
	}
	fu_usb_device_add_interface(FU_USB_DEVICE(self), self->iface_hid);
	return TRUE;
}

static gboolean
fu_jabra_gnp_device_tx_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuJabraGnpDevice *self = FU_JABRA_GNP_DEVICE(device);
	FuJabraGnpTxData *tx_data = (FuJabraGnpTxData *)user_data;
	FuUsbDevice *target = self->address == FU_JABRA_GNP_ADDRESS_OTA_CHILD
				  ? FU_USB_DEVICE(fu_device_get_parent(device))
				  : FU_USB_DEVICE(self);

	if (!fu_hid_device_set_report(FU_HID_DEVICE(self),
				      0x0,
				      tx_data->txbuf,
				      FU_JABRA_GNP_BUF_SIZE,
				      tx_data->timeout,
				      FU_HID_DEVICE_FLAG_RETRY_FAILURE |
					  FU_HID_DEVICE_FLAG_AUTODETECT_EPS,
				      error)) {
		g_prefix_error(error, "failed to write to device: ");
		return FALSE;
	}

	// if (!fu_usb_device_control_transfer(FU_USB_DEVICE(target),
	// 				    FU_USB_DIRECTION_HOST_TO_DEVICE,
	// 				    FU_USB_REQUEST_TYPE_CLASS,
	// 				    FU_USB_RECIPIENT_INTERFACE,
	// 				    0x09,
	// 				    0x0200 | FU_JABRA_GNP_IFACE,
	// 				    self->iface_hid,
	// 				    tx_data->txbuf,
	// 				    FU_JABRA_GNP_BUF_SIZE,
	// 				    NULL,
	// 				    tx_data->timeout,
	// 				    NULL, /* cancellable */
	// 				    error)) {
	// 	g_prefix_error(error, "failed to write to device: ");
	// 	return FALSE;
	// }
	// if (tx_data->txbuf[5] != 0x0F || tx_data->txbuf[6] != 0x1A)
	g_info("sent: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X "
	       "%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X "
	       "%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X "
	       "%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X "
	       "%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X "
	       "%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
	       tx_data->txbuf[0],
	       tx_data->txbuf[1],
	       tx_data->txbuf[2],
	       tx_data->txbuf[3],
	       tx_data->txbuf[4],
	       tx_data->txbuf[5],
	       tx_data->txbuf[6],
	       tx_data->txbuf[7],
	       tx_data->txbuf[8],
	       tx_data->txbuf[9],
	       tx_data->txbuf[10],
	       tx_data->txbuf[11],
	       tx_data->txbuf[12],
	       tx_data->txbuf[13],
	       tx_data->txbuf[14],
	       tx_data->txbuf[15],
	       tx_data->txbuf[16],
	       tx_data->txbuf[17],
	       tx_data->txbuf[18],
	       tx_data->txbuf[19],
	       tx_data->txbuf[20],
	       tx_data->txbuf[21],
	       tx_data->txbuf[22],
	       tx_data->txbuf[23],
	       tx_data->txbuf[24],
	       tx_data->txbuf[25],
	       tx_data->txbuf[26],
	       tx_data->txbuf[27],
	       tx_data->txbuf[28],
	       tx_data->txbuf[29],
	       tx_data->txbuf[30],
	       tx_data->txbuf[31],
	       tx_data->txbuf[32],
	       tx_data->txbuf[33],
	       tx_data->txbuf[34],
	       tx_data->txbuf[35],
	       tx_data->txbuf[36],
	       tx_data->txbuf[37],
	       tx_data->txbuf[38],
	       tx_data->txbuf[39],
	       tx_data->txbuf[40],
	       tx_data->txbuf[41],
	       tx_data->txbuf[42],
	       tx_data->txbuf[43],
	       tx_data->txbuf[44],
	       tx_data->txbuf[45],
	       tx_data->txbuf[46],
	       tx_data->txbuf[47],
	       tx_data->txbuf[48],
	       tx_data->txbuf[49],
	       tx_data->txbuf[50],
	       tx_data->txbuf[51],
	       tx_data->txbuf[52],
	       tx_data->txbuf[53],
	       tx_data->txbuf[54],
	       tx_data->txbuf[55],
	       tx_data->txbuf[56],
	       tx_data->txbuf[57],
	       tx_data->txbuf[58],
	       tx_data->txbuf[59],
	       tx_data->txbuf[60],
	       tx_data->txbuf[61],
	       tx_data->txbuf[62],
	       tx_data->txbuf[63]);

	return TRUE;
}

static gboolean
fu_jabra_gnp_device_rx_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuJabraGnpDevice *self = FU_JABRA_GNP_DEVICE(device);
	const guint8 match_buf[FU_JABRA_GNP_BUF_SIZE] =
	    {FU_JABRA_GNP_IFACE, 0x00, self->address, 0x00, 0x0A, 0x12, 0x02};
	const guint8 empty_buf[FU_JABRA_GNP_BUF_SIZE] = {0x00};
	FuJabraGnpRxData *rx_data = (FuJabraGnpRxData *)user_data;
	FuUsbDevice *target = self->address == FU_JABRA_GNP_ADDRESS_OTA_CHILD
				  ? FU_USB_DEVICE(fu_device_get_parent(device))
				  : FU_USB_DEVICE(self);

	if (!fu_hid_device_get_report(FU_HID_DEVICE(self),
				      0x00,
				      rx_data->rxbuf,
				      FU_JABRA_GNP_BUF_SIZE,
				      rx_data->timeout,
				      FU_HID_DEVICE_FLAG_AUTODETECT_EPS |
					  FU_HID_DEVICE_FLAG_RETRY_FAILURE |
					  FU_HID_DEVICE_FLAG_USE_INTERRUPT_TRANSFER,
				      error)) {
		g_prefix_error(error, "failed to get payload response: ");
		return FALSE;
	}
	// if (!fu_usb_device_interrupt_transfer(target,
	// 				      self->epin,
	// 				      rx_data->rxbuf,
	// 				      FU_JABRA_GNP_BUF_SIZE,
	// 				      NULL,
	// 				      rx_data->timeout,
	// 				      NULL, /* cancellable */
	// 				      error)) {
	// 	g_prefix_error(error, "failed to read from device: ");
	// 	return FALSE;
	// }
	if (rx_data->rxbuf[5] == match_buf[5] && rx_data->rxbuf[6] == match_buf[6]) {
		/* battery report, ignore and rx again */
		if (!fu_hid_device_get_report(FU_HID_DEVICE(self),
					      0x00,
					      rx_data->rxbuf,
					      sizeof(rx_data->rxbuf) / sizeof(rx_data->rxbuf[0]),
					      rx_data->timeout,
					      FU_HID_DEVICE_FLAG_AUTODETECT_EPS |
						  FU_HID_DEVICE_FLAG_RETRY_FAILURE |
						  FU_HID_DEVICE_FLAG_USE_INTERRUPT_TRANSFER,
					      error)) {
			g_prefix_error(error, "failed to get payload response: ");
			return FALSE;
		}
		// if (!fu_usb_device_interrupt_transfer(target,
		// 				      self->epin,
		// 				      rx_data->rxbuf,
		// 				      FU_JABRA_GNP_BUF_SIZE,
		// 				      NULL,
		// 				      rx_data->timeout,
		// 				      NULL, /* cancellable */
		// 				      error)) {
		// 	g_prefix_error(error, "failed to read from device: ");
		// 	return FALSE;
		// }
	}
	// if (rx_data->rxbuf[5] != 0x0F || rx_data->rxbuf[6] != 0x1B)
	g_info("received: %02X %02X %02X %02X %02X %02X %02X %02X %02X "
	       "%02X "
	       "%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X "
	       "%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X "
	       "%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X "
	       "%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X "
	       "%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
	       rx_data->rxbuf[0],
	       rx_data->rxbuf[1],
	       rx_data->rxbuf[2],
	       rx_data->rxbuf[3],
	       rx_data->rxbuf[4],
	       rx_data->rxbuf[5],
	       rx_data->rxbuf[6],
	       rx_data->rxbuf[7],
	       rx_data->rxbuf[8],
	       rx_data->rxbuf[9],
	       rx_data->rxbuf[10],
	       rx_data->rxbuf[11],
	       rx_data->rxbuf[12],
	       rx_data->rxbuf[13],
	       rx_data->rxbuf[14],
	       rx_data->rxbuf[15],
	       rx_data->rxbuf[16],
	       rx_data->rxbuf[17],
	       rx_data->rxbuf[18],
	       rx_data->rxbuf[19],
	       rx_data->rxbuf[20],
	       rx_data->rxbuf[21],
	       rx_data->rxbuf[22],
	       rx_data->rxbuf[23],
	       rx_data->rxbuf[24],
	       rx_data->rxbuf[25],
	       rx_data->rxbuf[26],
	       rx_data->rxbuf[27],
	       rx_data->rxbuf[28],
	       rx_data->rxbuf[29],
	       rx_data->rxbuf[30],
	       rx_data->rxbuf[31],
	       rx_data->rxbuf[32],
	       rx_data->rxbuf[33],
	       rx_data->rxbuf[34],
	       rx_data->rxbuf[35],
	       rx_data->rxbuf[36],
	       rx_data->rxbuf[37],
	       rx_data->rxbuf[38],
	       rx_data->rxbuf[39],
	       rx_data->rxbuf[40],
	       rx_data->rxbuf[41],
	       rx_data->rxbuf[42],
	       rx_data->rxbuf[43],
	       rx_data->rxbuf[44],
	       rx_data->rxbuf[45],
	       rx_data->rxbuf[46],
	       rx_data->rxbuf[47],
	       rx_data->rxbuf[48],
	       rx_data->rxbuf[49],
	       rx_data->rxbuf[50],
	       rx_data->rxbuf[51],
	       rx_data->rxbuf[52],
	       rx_data->rxbuf[53],
	       rx_data->rxbuf[54],
	       rx_data->rxbuf[55],
	       rx_data->rxbuf[56],
	       rx_data->rxbuf[57],
	       rx_data->rxbuf[58],
	       rx_data->rxbuf[59],
	       rx_data->rxbuf[60],
	       rx_data->rxbuf[61],
	       rx_data->rxbuf[62],
	       rx_data->rxbuf[63]);

	if (fu_memcmp_safe(rx_data->rxbuf,
			   sizeof(rx_data->rxbuf),
			   0,
			   empty_buf,
			   sizeof(rx_data->rxbuf),
			   0,
			   sizeof(rx_data->rxbuf),
			   error)) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "error reading from device");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_jabra_gnp_device_rx_with_sequence_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuJabraGnpDevice *self = FU_JABRA_GNP_DEVICE(device);
	FuJabraGnpRxData *rx_data = (FuJabraGnpRxData *)user_data;

	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_jabra_gnp_device_rx_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  rx_data,
				  error))
		return FALSE;
	if (self->sequence_number != rx_data->rxbuf[3]) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "sequence_number error -- got 0x%x, expected 0x%x",
			    rx_data->rxbuf[3],
			    self->sequence_number);
		return FALSE;
	}
	self->sequence_number += 1;
	return TRUE;
}

static gboolean
fu_jabra_gnp_device_read_name(FuJabraGnpDevice *self, GError **error)
{
	FuJabraGnpTxData tx_data = {
	    .txbuf =
		{
		    FU_JABRA_GNP_IFACE,
		    self->address,
		    0x00,
		    self->sequence_number,
		    0x46,
		    0x02,
		    0x00,
		},
	    .timeout = FU_JABRA_GNP_STANDARD_SEND_TIMEOUT,
	};
	FuJabraGnpRxData rx_data = {
	    .rxbuf = {0x00},
	    .timeout = FU_JABRA_GNP_STANDARD_RECEIVE_TIMEOUT,
	};
	g_autofree gchar *name = NULL;
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_jabra_gnp_device_tx_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  &tx_data,
				  error))
		return FALSE;
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_jabra_gnp_device_rx_with_sequence_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  &rx_data,
				  error))
		return FALSE;
	name = fu_memstrsafe(rx_data.rxbuf,
			     sizeof(rx_data.rxbuf),
			     0x8,
			     sizeof(rx_data.rxbuf) - 8,
			     error);
	if (name == NULL)
		return FALSE;
	fu_device_set_name(FU_DEVICE(self), name);
	return TRUE;
}

static gboolean
fu_jabra_gnp_device_read_child_dfu_pid(FuJabraGnpDevice *self,
				       guint16 *child_dfu_pid,
				       GError **error)
{
	FuJabraGnpTxData tx_data = {
	    .txbuf =
		{
		    FU_JABRA_GNP_IFACE,
		    FU_JABRA_GNP_ADDRESS_OTA_CHILD,
		    0x00,
		    self->sequence_number,
		    0x46,
		    0x02,
		    0x13,
		},
	    .timeout = FU_JABRA_GNP_STANDARD_SEND_TIMEOUT,
	};
	FuJabraGnpRxData rx_data = {
	    .rxbuf = {0x00},
	    .timeout = FU_JABRA_GNP_STANDARD_RECEIVE_TIMEOUT,
	};

	g_return_val_if_fail(child_dfu_pid != NULL, FALSE);

	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_jabra_gnp_device_tx_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  &tx_data,
				  error))
		return FALSE;
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_jabra_gnp_device_rx_with_sequence_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  &rx_data,
				  error))
		return FALSE;
	/* no child device to respond properly */
	if (rx_data.rxbuf[5] == 0xFE && (rx_data.rxbuf[6] == 0xF4 || rx_data.rxbuf[6] == 0xF3)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "internal error: no child device responded");
		return FALSE;
	}

	/* success */
	*child_dfu_pid = fu_memread_uint16(rx_data.rxbuf + 7, G_LITTLE_ENDIAN);
	return TRUE;
}

static gboolean
fu_jabra_gnp_device_read_dfu_pid(FuJabraGnpDevice *self, GError **error)
{
	FuJabraGnpTxData tx_data = {
	    .txbuf =
		{
		    FU_JABRA_GNP_IFACE,
		    self->address,
		    0x00,
		    self->sequence_number,
		    0x46,
		    0x02,
		    0x13,
		},
	    .timeout = FU_JABRA_GNP_STANDARD_SEND_TIMEOUT,
	};
	FuJabraGnpRxData rx_data = {
	    .rxbuf = {0x00},
	    .timeout = FU_JABRA_GNP_STANDARD_RECEIVE_TIMEOUT,
	};

	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_jabra_gnp_device_tx_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  &tx_data,
				  error))
		return FALSE;
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_jabra_gnp_device_rx_with_sequence_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  &rx_data,
				  error))
		return FALSE;
	self->dfu_pid = fu_memread_uint16(rx_data.rxbuf + 7, G_LITTLE_ENDIAN);
	return TRUE;
}

static gboolean
fu_jabra_gnp_device_read_version(FuJabraGnpDevice *self, GError **error)
{
	FuJabraGnpTxData tx_data = {
	    .txbuf =
		{
		    FU_JABRA_GNP_IFACE,
		    self->address,
		    0x00,
		    self->sequence_number,
		    0x46,
		    0x02,
		    0x03,
		},
	    .timeout = FU_JABRA_GNP_STANDARD_SEND_TIMEOUT,
	};
	FuJabraGnpRxData rx_data = {
	    .rxbuf = {0x00},
	    .timeout = FU_JABRA_GNP_STANDARD_RECEIVE_TIMEOUT,
	};
	g_autofree gchar *version = NULL;
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_jabra_gnp_device_tx_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  &tx_data,
				  error))
		return FALSE;
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_jabra_gnp_device_rx_with_sequence_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  &rx_data,
				  error))
		return FALSE;

	version = fu_memstrsafe(rx_data.rxbuf,
				sizeof(rx_data.rxbuf),
				0x8,
				sizeof(rx_data.rxbuf) - 8,
				error);
	if (version == NULL)
		return FALSE;

	/* some devices append a few extra non number characters to the version, which can confuse
	 * fwupd's formats, so remove it */
	while (!(g_str_has_suffix(version, "0") || g_str_has_suffix(version, "1") ||
		 g_str_has_suffix(version, "2") || g_str_has_suffix(version, "3") ||
		 g_str_has_suffix(version, "4") || g_str_has_suffix(version, "5") ||
		 g_str_has_suffix(version, "6") || g_str_has_suffix(version, "7") ||
		 g_str_has_suffix(version, "8") || g_str_has_suffix(version, "9")))
		version[strlen(version) - 1] = '\0';

	fu_device_set_version(FU_DEVICE(self), version);
	return TRUE;
}

static gboolean
fu_jabra_gnp_device_read_fwu_protocol(FuJabraGnpDevice *self, GError **error)
{
	FuJabraGnpTxData tx_data = {
	    .txbuf =
		{
		    FU_JABRA_GNP_IFACE,
		    self->address,
		    0x00,
		    self->sequence_number,
		    0x46,
		    0x02,
		    0x14,
		},
	    .timeout = FU_JABRA_GNP_STANDARD_SEND_TIMEOUT,
	};
	FuJabraGnpRxData rx_data = {
	    .rxbuf = {0x00},
	    .timeout = FU_JABRA_GNP_STANDARD_RECEIVE_TIMEOUT,
	};
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_jabra_gnp_device_tx_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  &tx_data,
				  error))
		return FALSE;
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_jabra_gnp_device_rx_with_sequence_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  &rx_data,
				  error))
		return FALSE;
	if (rx_data.rxbuf[7] != FU_JABRA_GNP_PROTOCOL_OTA &&
	    rx_data.rxbuf[7] != FU_JABRA_GNP_PROTOCOL_EXTENDED_OTA) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "unrecognized protocol: expected 7 or 16, got %d",
			    rx_data.rxbuf[7]);
		return FALSE;
	}
	self->fwu_protocol = rx_data.rxbuf[7];
	return TRUE;
}

static gboolean
fu_jabra_gnp_device_write_partition(FuJabraGnpDevice *self, guint8 part, GError **error)
{
	FuJabraGnpTxData tx_data = {
	    .txbuf =
		{
		    FU_JABRA_GNP_IFACE,
		    self->address,
		    0x00,
		    self->sequence_number,
		    0x87,
		    0x0F,
		    0x2D,
		    part,
		},
	    .timeout = FU_JABRA_GNP_STANDARD_SEND_TIMEOUT,
	};
	FuJabraGnpRxData rx_data = {
	    .rxbuf = {0x00},
	    .timeout = FU_JABRA_GNP_STANDARD_RECEIVE_TIMEOUT,
	};
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_jabra_gnp_device_tx_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  &tx_data,
				  error))
		return FALSE;
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_jabra_gnp_device_rx_with_sequence_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  &rx_data,
				  error))
		return FALSE;
	if (rx_data.rxbuf[5] != 0xFF) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "internal error: expected 0xFF, got 0x%02x 0x%02x",
			    rx_data.rxbuf[5],
			    rx_data.rxbuf[6]);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_jabra_gnp_device_start(FuJabraGnpDevice *self, GError **error)
{
	FuJabraGnpTxData tx_data = {
	    .txbuf =
		{
		    FU_JABRA_GNP_IFACE,
		    self->address,
		    0x00,
		    self->sequence_number,
		    0x86,
		    0x0F,
		    0x17,
		},
	    .timeout = FU_JABRA_GNP_STANDARD_SEND_TIMEOUT,
	};
	FuJabraGnpRxData rx_data = {
	    .rxbuf = {0x00},
	    .timeout = FU_JABRA_GNP_STANDARD_RECEIVE_TIMEOUT,
	};
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_jabra_gnp_device_tx_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  &tx_data,
				  error))
		return FALSE;
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_jabra_gnp_device_rx_with_sequence_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  &rx_data,
				  error))
		return FALSE;
	if (rx_data.rxbuf[5] != 0xFF) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "internal error: expected 0xFF, got 0x%02x 0x%02x",
			    rx_data.rxbuf[5],
			    rx_data.rxbuf[6]);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_jabra_gnp_device_flash_erase_done(FuJabraGnpDevice *self, GError **error)
{
	const guint8 match_buf[FU_JABRA_GNP_BUF_SIZE] =
	    {FU_JABRA_GNP_IFACE, 0x00, self->address, 0x00, 0x06, 0x0F, 0x18};
	FuJabraGnpRxData rx_data = {
	    .rxbuf = {0x00},
	    .timeout = FU_JABRA_GNP_EXTRA_LONG_RECEIVE_TIMEOUT,
	};
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_jabra_gnp_device_rx_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  &rx_data,
				  error))
		return FALSE;
	if (rx_data.rxbuf[5] != match_buf[5] || rx_data.rxbuf[6] != match_buf[6]) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "internal error, buf did not match");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_jabra_gnp_device_write_crc(FuJabraGnpDevice *self,
			      guint32 crc,
			      guint total_chunks,
			      guint preload_count,
			      GError **error)
{
	FuJabraGnpTxData tx_data = {
	    .txbuf =
		{
		    FU_JABRA_GNP_IFACE,
		    self->address,
		    0x00,
		    self->sequence_number,
		    0x8E,
		    0x0F,
		    0x19,
		},
	    .timeout = FU_JABRA_GNP_STANDARD_SEND_TIMEOUT,
	};
	FuJabraGnpRxData rx_data = {
	    .rxbuf = {0x00},
	    .timeout = FU_JABRA_GNP_STANDARD_RECEIVE_TIMEOUT,
	};

	fu_memwrite_uint32(tx_data.txbuf + 7, crc, G_LITTLE_ENDIAN);
	fu_memwrite_uint16(tx_data.txbuf + 11, total_chunks, G_LITTLE_ENDIAN);
	fu_memwrite_uint16(tx_data.txbuf + 13, preload_count, G_LITTLE_ENDIAN);

	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_jabra_gnp_device_tx_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  &tx_data,
				  error))
		return FALSE;
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_jabra_gnp_device_rx_with_sequence_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  &rx_data,
				  error))
		return FALSE;
	if (rx_data.rxbuf[5] != 0xFF) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "internal error: expected 0xFF, got 0x%02x 0x%02x",
			    rx_data.rxbuf[5],
			    rx_data.rxbuf[6]);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_jabra_gnp_device_write_extended_crc(FuJabraGnpDevice *self,
				       guint32 crc,
				       guint total_chunks,
				       guint preload_count,
				       GError **error)
{
	FuJabraGnpTxData tx_data = {
	    .txbuf =
		{
		    FU_JABRA_GNP_IFACE,
		    self->address,
		    0x00,
		    self->sequence_number,
		    0x92,
		    0x0F,
		    0x19,
		},
	    .timeout = FU_JABRA_GNP_STANDARD_SEND_TIMEOUT,
	};
	FuJabraGnpRxData rx_data = {
	    .rxbuf = {0x00},
	    .timeout = FU_JABRA_GNP_STANDARD_RECEIVE_TIMEOUT,
	};

	fu_memwrite_uint8_safe(tx_data.txbuf, 64, 9, crc, error);
	fu_memwrite_uint8_safe(tx_data.txbuf, 64, 10, crc >> 8, error);
	fu_memwrite_uint8_safe(tx_data.txbuf, 64, 7, crc >> 16, error);
	fu_memwrite_uint8_safe(tx_data.txbuf, 64, 8, crc >> 24, error);
	fu_memwrite_uint16(tx_data.txbuf + 11, 0x00, G_LITTLE_ENDIAN);
	fu_memwrite_uint16(tx_data.txbuf + 13, preload_count, G_LITTLE_ENDIAN);
	fu_memwrite_uint8_safe(tx_data.txbuf, 64, 15, total_chunks, error);
	fu_memwrite_uint8_safe(tx_data.txbuf, 64, 16, total_chunks >> 8, error);
	fu_memwrite_uint8_safe(tx_data.txbuf, 64, 17, total_chunks >> 16, error);
	fu_memwrite_uint8_safe(tx_data.txbuf, 64, 18, total_chunks >> 24, error);

	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_jabra_gnp_device_tx_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  &tx_data,
				  error))
		return FALSE;
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_jabra_gnp_device_rx_with_sequence_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  &rx_data,
				  error))
		return FALSE;
	if (rx_data.rxbuf[5] != 0xFF) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "internal error: expected 0xFF, got 0x%02x 0x%02x",
			    rx_data.rxbuf[5],
			    rx_data.rxbuf[6]);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_jabra_gnp_device_write_chunk(FuJabraGnpDevice *self,
				guint32 chunk_number,
				const guint8 *buf,
				gsize bufsz,
				GError **error)
{
	guint8 write_length = 0x00 + bufsz + 10;
	FuJabraGnpTxData tx_data = {
	    .txbuf =
		{
		    FU_JABRA_GNP_IFACE,
		    self->address,
		    0x00,
		    0x00,
		    write_length,
		    0x0F,
		    0x1A,
		},
	    .timeout = FU_JABRA_GNP_STANDARD_SEND_TIMEOUT,
	};
	fu_memwrite_uint16(tx_data.txbuf + 7, chunk_number, G_LITTLE_ENDIAN);
	fu_memwrite_uint16(tx_data.txbuf + 9, bufsz, G_LITTLE_ENDIAN);
	if (!fu_memcpy_safe(tx_data.txbuf,
			    sizeof(tx_data.txbuf),
			    11,
			    buf,
			    bufsz,
			    0x0,
			    bufsz,
			    error))
		return FALSE;
	return fu_device_retry_full(FU_DEVICE(self),
				    fu_jabra_gnp_device_tx_cb,
				    FU_JABRA_GNP_MAX_RETRIES,
				    FU_JABRA_GNP_RETRY_DELAY,
				    &tx_data,
				    error);
}

static gboolean
fu_jabra_gnp_device_write_chunks(FuJabraGnpDevice *self,
				 FuChunkArray *chunks,
				 FuProgress *progress,
				 GError **error)
{
	gboolean failed_chunk = FALSE;
	g_autoptr(FuChunk) ini_chk = NULL;

	const guint8 match_buf[FU_JABRA_GNP_BUF_SIZE] = {
	    FU_JABRA_GNP_IFACE,
	    0x00,
	    self->address,
	    0x00,
	    0x06,
	    0x0F,
	    0x1B,
	};
	FuJabraGnpRxData rx_data = {
	    .rxbuf = {0x00},
	    .timeout = FU_JABRA_GNP_LONG_RECEIVE_TIMEOUT,
	};

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));

	ini_chk = fu_chunk_array_index(chunks, 0, error);
	if (!fu_jabra_gnp_device_write_chunk(self,
					     0,
					     fu_chunk_get_data(ini_chk),
					     fu_chunk_get_data_sz(ini_chk),
					     error))
		return FALSE;

	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_jabra_gnp_device_rx_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  &rx_data,
				  error))
		return FALSE;

	for (gint chunk_number = 1; (guint)chunk_number < fu_chunk_array_length(chunks);
	     chunk_number++) {
		g_autoptr(FuChunk) chk = NULL;

		/* prepare chunk */
		chk = fu_chunk_array_index(chunks, chunk_number, error);
		if (chk == NULL)
			return FALSE;
		if (!fu_jabra_gnp_device_write_chunk(self,
						     chunk_number,
						     fu_chunk_get_data(chk),
						     fu_chunk_get_data_sz(chk),
						     error))
			return FALSE;
		if (((chunk_number % FU_JABRA_GNP_PRELOAD_COUNT) == 0))
			if (!fu_device_retry_full(FU_DEVICE(self),
						  fu_jabra_gnp_device_rx_cb,
						  FU_JABRA_GNP_MAX_RETRIES,
						  FU_JABRA_GNP_RETRY_DELAY,
						  &rx_data,
						  error))
				return FALSE;

		if ((guint)chunk_number == fu_chunk_array_length(chunks) - 1)
			if (!fu_device_retry_full(FU_DEVICE(self),
						  fu_jabra_gnp_device_rx_cb,
						  FU_JABRA_GNP_MAX_RETRIES,
						  FU_JABRA_GNP_RETRY_DELAY,
						  &rx_data,
						  error))
				return FALSE;

		if (!failed_chunk)
			fu_progress_step_done(progress);
		if (chunk_number % 100 == 0)
			g_info("tx chunk: 0x%x", chunk_number);
	}
	/* success */
	return TRUE;
}

static gboolean
fu_jabra_gnp_device_read_verify_status(FuJabraGnpDevice *self, GError **error)
{
	const guint8 match_buf[FU_JABRA_GNP_BUF_SIZE] =
	    {FU_JABRA_GNP_IFACE, 0x00, self->address, 0x00, 0x06, 0x0F, 0x1C};
	FuJabraGnpRxData rx_data = {
	    .rxbuf = {0x00},
	    .timeout = FU_JABRA_GNP_STANDARD_RECEIVE_TIMEOUT,
	};
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_jabra_gnp_device_rx_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  &rx_data,
				  error))
		return FALSE;
	if (rx_data.rxbuf[5] != match_buf[5] || rx_data.rxbuf[6] != match_buf[6]) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "internal error, buf did not match");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_jabra_gnp_device_write_version(FuJabraGnpDevice *self,
				  FuJabraGnpVersionData *version_data,
				  GError **error)
{
	FuJabraGnpTxData tx_data = {
	    .txbuf =
		{
		    FU_JABRA_GNP_IFACE,
		    self->address,
		    0x00,
		    self->sequence_number,
		    0x89,
		    0x0F,
		    0x1E,
		    version_data->major,
		    version_data->minor,
		    version_data->micro,
		},
	    .timeout = FU_JABRA_GNP_STANDARD_SEND_TIMEOUT,
	};
	FuJabraGnpRxData rx_data = {
	    .rxbuf = {0x00},
	    .timeout = FU_JABRA_GNP_STANDARD_RECEIVE_TIMEOUT,
	};
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_jabra_gnp_device_tx_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  &tx_data,
				  error))
		return FALSE;
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_jabra_gnp_device_rx_with_sequence_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  &rx_data,
				  error))
		return FALSE;
	if (rx_data.rxbuf[5] != 0xFF) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "internal error: expected 0xFF, got 0x%02x 0x%02x",
			    rx_data.rxbuf[5],
			    rx_data.rxbuf[6]);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_jabra_gnp_device_write_dfu_from_squif(FuJabraGnpDevice *self, GError **error)
{
	FuJabraGnpTxData tx_data = {
	    .txbuf =
		{
		    FU_JABRA_GNP_IFACE,
		    self->address,
		    0x00,
		    self->sequence_number,
		    0x86,
		    0x0F,
		    0x1D,
		},
	    .timeout = FU_JABRA_GNP_STANDARD_SEND_TIMEOUT,
	};
	FuJabraGnpRxData rx_data = {
	    .rxbuf = {0x00},
	    .timeout = FU_JABRA_GNP_STANDARD_RECEIVE_TIMEOUT,
	};
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_jabra_gnp_device_tx_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  &tx_data,
				  error))
		return FALSE;
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_jabra_gnp_device_rx_with_sequence_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  &rx_data,
				  error))
		return FALSE;
	if (rx_data.rxbuf[5] != 0xFF) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "internal error: expected 0xFF, got 0x%02x 0x%02x",
			    rx_data.rxbuf[5],
			    rx_data.rxbuf[6]);
		return FALSE;
	}
	return TRUE;
}

static FuFirmware *
fu_jabra_gnp_device_prepare_firmware(FuDevice *device,
				     GInputStream *stream,
				     FuProgress *progress,
				     FuFirmwareParseFlags flags,
				     GError **error)
{
	FuJabraGnpDevice *self = FU_JABRA_GNP_DEVICE(device);
	g_autoptr(FuFirmware) firmware = fu_jabra_gnp_firmware_new();

	/* unzip and get images */
	if (!fu_firmware_parse_stream(firmware, stream, 0x0, flags, error))
		return NULL;
	if (fu_jabra_gnp_firmware_get_dfu_pid(FU_JABRA_GNP_FIRMWARE(firmware)) != self->dfu_pid) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "wrong DFU PID, got 0x%x, expected 0x%x",
			    fu_jabra_gnp_firmware_get_dfu_pid(FU_JABRA_GNP_FIRMWARE(firmware)),
			    self->dfu_pid);
		return NULL;
	}
	return g_steal_pointer(&firmware);
}

static gboolean
fu_jabra_gnp_device_add_child(FuDevice *device, guint16 dfu_pid, GError **error)
{
	FuJabraGnpDevice *self = FU_JABRA_GNP_DEVICE(device);
	g_autoptr(FuJabraGnpChildDevice) child = NULL;

	/* sanity check */
	if (self->address != FU_JABRA_GNP_ADDRESS_PARENT) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "expected address 0x%x, and got 0x%x",
			    (guint)FU_JABRA_GNP_ADDRESS_OTA_CHILD,
			    self->address);
		return FALSE;
	}

	child = g_object_new(FU_TYPE_JABRA_GNP_CHILD_DEVICE, "parent", FU_DEVICE(self), NULL);
	fu_jabra_gnp_child_device_set_dfu_pid_and_seq(child, dfu_pid);
	fu_device_incorporate(FU_DEVICE(child),
			      FU_DEVICE(self),
			      FU_DEVICE_INCORPORATE_FLAG_PHYSICAL_ID);
	if (!fu_device_setup(FU_DEVICE(child), error)) {
		g_prefix_error(error, "failed to setup child device: ");
		return FALSE;
	}

	fu_device_add_instance_u16(FU_DEVICE(child), "VID", fu_device_get_vid(FU_DEVICE(self)));
	fu_device_add_instance_u16(FU_DEVICE(child), "PID", dfu_pid);
	if (!fu_device_build_instance_id_full(FU_DEVICE(child),
					      FU_DEVICE_INSTANCE_FLAG_QUIRKS |
						  FU_DEVICE_INSTANCE_FLAG_VISIBLE,
					      error,
					      "USB",
					      "VID",
					      "PID",
					      NULL))
		return FALSE;

	fu_device_add_child(FU_DEVICE(self), FU_DEVICE(child));

	/* success */
	return TRUE;
}

static gboolean
fu_jabra_gnp_device_setup(FuDevice *device, GError **error)
{
	FuJabraGnpDevice *self = FU_JABRA_GNP_DEVICE(device);
	g_autoptr(GError) error_local = NULL;
	if (!fu_jabra_gnp_device_read_name(self, error))
		return FALSE;
	if (!fu_jabra_gnp_device_read_version(self, error))
		return FALSE;
	if (!fu_jabra_gnp_device_read_dfu_pid(self, error))
		return FALSE;
	if (self->address == FU_JABRA_GNP_ADDRESS_PARENT) {
		guint16 child_dfu_pid = 0;
		if (!fu_jabra_gnp_device_read_child_dfu_pid(self, &child_dfu_pid, &error_local)) {
			g_debug("unable to read child's PID, %s", error_local->message);
			return TRUE;
		}
		if (child_dfu_pid > 0x0) {
			if (!fu_jabra_gnp_device_add_child(FU_DEVICE(self), child_dfu_pid, error)) {
				g_prefix_error(
				    error,
				    "found child device with PID 0x%x, but failed to add as child "
				    "of parent with PID 0x%x, unpair or turn off child device to "
				    "update parent device: ",
				    child_dfu_pid,
				    self->dfu_pid);
				return FALSE;
			}
		}
	}
	return TRUE;
}

static gboolean
fu_jabra_gnp_device_write_image(FuJabraGnpDevice *self,
				FuFirmware *firmware,
				FuFirmware *img,
				FuProgress *progress,
				GError **error)
{
	const guint chunk_size = 52;
	g_autoptr(FuChunkArray) chunks = NULL;
	g_autoptr(GInputStream) stream = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 1, "write-partition");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 1, "start");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 5, "flash-erase-done");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 91, "write-chunks");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 1, "read-verify-status");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 1, "write-version");

	/* write partition */
	stream = fu_firmware_get_stream(img, error);
	if (stream == NULL)
		return FALSE;
	if (!fu_jabra_gnp_device_write_partition(self, fu_firmware_get_idx(img), error))
		return FALSE;
	fu_progress_step_done(progress);

	/* start erasing */
	if (!fu_jabra_gnp_device_start(self, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* poll for erase done */
	if (!fu_jabra_gnp_device_flash_erase_done(self, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* write chunks */
	chunks = fu_chunk_array_new_from_stream(stream,
						FU_CHUNK_ADDR_OFFSET_NONE,
						FU_CHUNK_PAGESZ_NONE,
						chunk_size,
						error);
	if (chunks == NULL)
		return FALSE;
	if (self->fwu_protocol == FU_JABRA_GNP_PROTOCOL_OTA) {
		if (!fu_jabra_gnp_device_write_crc(
			self,
			fu_jabra_gnp_image_get_crc32(FU_JABRA_GNP_IMAGE(img)),
			fu_chunk_array_length(chunks),
			FU_JABRA_GNP_PRELOAD_COUNT,
			error))
			return FALSE;
	} else {
		/* FU_JABRA_GNP_PROTOCOL_EXTENDED_OTA */
		if (!fu_jabra_gnp_device_write_extended_crc(
			self,
			fu_jabra_gnp_image_get_crc32(FU_JABRA_GNP_IMAGE(img)),
			fu_chunk_array_length(chunks),
			FU_JABRA_GNP_PRELOAD_COUNT,
			error))
			return FALSE;
	}
	if (!fu_jabra_gnp_device_write_chunks(self, chunks, fu_progress_get_child(progress), error))
		return FALSE;
	fu_progress_step_done(progress);

	/* verify */
	if (!fu_jabra_gnp_device_read_verify_status(self, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* write version */
	if (!fu_jabra_gnp_device_write_version(
		self,
		fu_jabra_gnp_firmware_get_version_data(FU_JABRA_GNP_FIRMWARE(firmware)),
		error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static gboolean
fu_jabra_gnp_device_write_firmware(FuDevice *device,
				   FuFirmware *firmware,
				   FuProgress *progress,
				   FwupdInstallFlags flags,
				   GError **error)
{
	FuJabraGnpDevice *self = FU_JABRA_GNP_DEVICE(device);
	g_autoptr(GPtrArray) imgs = fu_firmware_get_images(firmware);

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_NO_PROFILE);
	for (guint i = 0; i < imgs->len; i++) {
		FuFirmware *img = g_ptr_array_index(imgs, i);
		fu_progress_add_step(progress,
				     FWUPD_STATUS_UNKNOWN,
				     fu_firmware_get_size(img),
				     fu_firmware_get_id(img));
	}
	if (!fu_jabra_gnp_device_read_fwu_protocol(self, error))
		return FALSE;
	for (guint i = 0; i < imgs->len; i++) {
		FuFirmware *img = g_ptr_array_index(imgs, i);
		if (!fu_jabra_gnp_device_write_image(self,
						     firmware,
						     img,
						     fu_progress_get_child(progress),
						     error)) {
			g_prefix_error(error, "failed to write %s: ", fu_firmware_get_id(img));
			return FALSE;
		}
		fu_progress_step_done(progress);
	}
	/* write squif */
	if (!fu_jabra_gnp_device_write_dfu_from_squif(self, error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_jabra_gnp_device_set_quirk_kv(FuDevice *device,
				 const gchar *key,
				 const gchar *value,
				 GError **error)
{
	FuJabraGnpDevice *self = FU_JABRA_GNP_DEVICE(device);

	if (g_strcmp0(key, "JabraGnpAddress") == 0) {
		guint64 val = 0;
		if (!fu_strtoull(value, &val, 0x0, G_MAXUINT8, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->address = (guint8)val;
		return TRUE;
	}

	/* failed */
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "quirk key not supported");
	return FALSE;
}

static void
fu_jabra_gnp_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 5, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 75, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 5, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 15, "reload");
}

static void
fu_jabra_gnp_device_init(FuJabraGnpDevice *self)
{
	self->address = 0x08;
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_DUAL_IMAGE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SELF_RECOVERY);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_ADD_COUNTERPART_GUIDS);
	fu_device_add_protocol(FU_DEVICE(self), "com.jabra.gnp");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_JABRA_GNP_FIRMWARE);
	fu_hid_device_add_flag(FU_HID_DEVICE(self), FU_HID_DEVICE_FLAG_RETRY_FAILURE);
	fu_hid_device_add_flag(FU_HID_DEVICE(self), FU_HID_DEVICE_FLAG_AUTODETECT_EPS);
}

static void
fu_jabra_gnp_device_class_init(FuJabraGnpDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->to_string = fu_jabra_gnp_device_to_string;
	device_class->prepare_firmware = fu_jabra_gnp_device_prepare_firmware;
	device_class->probe = fu_jabra_gnp_device_probe;
	device_class->setup = fu_jabra_gnp_device_setup;
	device_class->write_firmware = fu_jabra_gnp_device_write_firmware;
	device_class->set_quirk_kv = fu_jabra_gnp_device_set_quirk_kv;
	device_class->set_progress = fu_jabra_gnp_device_set_progress;
}
