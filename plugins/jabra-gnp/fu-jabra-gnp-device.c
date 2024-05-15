/*
 * Copyright (C) 2023 GN Audio
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-jabra-gnp-device.h"
#include "fu-jabra-gnp-firmware.h"
#include "fu-jabra-gnp-image.h"

#define FU_JABRA_GNP_BUF_SIZE			64
#define FU_JABRA_GNP_MAX_RETRIES		3
#define FU_JABRA_GNP_PRELOAD_COUNT		10
#define FU_JABRA_GNP_RETRY_DELAY		100   /* ms */
#define FU_JABRA_GNP_STANDARD_SEND_TIMEOUT	3000  /* ms */
#define FU_JABRA_GNP_STANDARD_RECEIVE_TIMEOUT	1000  /* ms */
#define FU_JABRA_GNP_LONG_RECEIVE_TIMEOUT	30000 /* ms */
#define FU_JABRA_GNP_EXTRA_LONG_RECEIVE_TIMEOUT 60000 /* ms */

#define FU_JABRA_GNP_IFACE 0x05

#define FU_JABRA_GNP_ADDRESS_PARENT    0x01
#define FU_JABRA_GNP_ADDRESS_OTA_CHILD 0x04

struct _FuJabraGnpDevice {
	FuUsbDevice parent_instance;
	guint8 iface_hid;
	guint8 sequence_number;
	guint8 address;
	guint dfu_pid;
};

typedef struct {
	guint8 txbuf[FU_JABRA_GNP_BUF_SIZE];
	const guint timeout;
} FuJabraGnpTxData;

typedef struct {
	guint8 rxbuf[FU_JABRA_GNP_BUF_SIZE];
	const guint timeout;
} FuJabraGnpRxData;

G_DEFINE_TYPE(FuJabraGnpDevice, fu_jabra_gnp_device, FU_TYPE_USB_DEVICE)

static void
fu_jabra_gnp_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuJabraGnpDevice *self = FU_JABRA_GNP_DEVICE(device);
	fu_string_append_kx(str, idt, "IfaceHid", self->iface_hid);
	fu_string_append_kx(str, idt, "SequenceNumber", self->sequence_number);
	fu_string_append_kx(str, idt, "Address", self->address);
	fu_string_append_kx(str, idt, "DfuPid", self->dfu_pid);
}

static guint8
_g_usb_device_get_interface_for_class(GUsbDevice *dev, guint8 intf_class, GError **error)
{
	g_autoptr(GPtrArray) intfs = NULL;
	intfs = g_usb_device_get_interfaces(dev, error);
	if (intfs == NULL)
		return 0xFF;
	for (guint i = 0; i < intfs->len; i++) {
		GUsbInterface *intf = g_ptr_array_index(intfs, i);
		if (g_usb_interface_get_class(intf) == intf_class)
			return g_usb_interface_get_number(intf);
	}
	return 0xFF;
}

static gboolean
fu_jabra_gnp_device_tx_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuJabraGnpDevice *self = FU_JABRA_GNP_DEVICE(device);
	FuJabraGnpTxData *tx_data = (FuJabraGnpTxData *)user_data;
	FuUsbDevice *target = self->address == FU_JABRA_GNP_ADDRESS_OTA_CHILD
				  ? FU_USB_DEVICE(fu_device_get_parent(device))
				  : FU_USB_DEVICE(self);
	if (!g_usb_device_control_transfer(fu_usb_device_get_dev(target),
					   G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					   G_USB_DEVICE_REQUEST_TYPE_CLASS,
					   G_USB_DEVICE_RECIPIENT_INTERFACE,
					   0x09,
					   0x0200 | FU_JABRA_GNP_IFACE,
					   self->iface_hid,
					   tx_data->txbuf,
					   FU_JABRA_GNP_BUF_SIZE,
					   NULL,
					   tx_data->timeout,
					   NULL, /* cancellable */
					   error)) {
		g_prefix_error(error, "failed to write to device: ");
		return FALSE;
	}
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

	if (!g_usb_device_interrupt_transfer(fu_usb_device_get_dev(target),
					     0x81,
					     rx_data->rxbuf,
					     FU_JABRA_GNP_BUF_SIZE,
					     NULL,
					     rx_data->timeout,
					     NULL, /* cancellable */
					     error)) {
		g_prefix_error(error, "failed to read from device: ");
		return FALSE;
	}
	if (rx_data->rxbuf[2] == match_buf[2] && rx_data->rxbuf[5] == match_buf[5] &&
	    rx_data->rxbuf[6] == match_buf[6]) {
		/* battery report, ignore and rx again */
		if (!g_usb_device_interrupt_transfer(fu_usb_device_get_dev(target),
						     0x81,
						     rx_data->rxbuf,
						     FU_JABRA_GNP_BUF_SIZE,
						     NULL,
						     rx_data->timeout,
						     NULL, /* cancellable */
						     error)) {
			g_prefix_error(error, "failed to read from device: ");
			return FALSE;
		}
	}

	if (fu_memcmp_safe(rx_data->rxbuf,
			   sizeof(rx_data->rxbuf),
			   0,
			   empty_buf,
			   sizeof(rx_data->rxbuf),
			   0,
			   sizeof(rx_data->rxbuf),
			   error)) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "error readuing from device");
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
				  (gpointer)&tx_data,
				  error))
		return FALSE;
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_jabra_gnp_device_rx_with_sequence_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  (gpointer)&rx_data,
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
				  (gpointer)&tx_data,
				  error))
		return FALSE;
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_jabra_gnp_device_rx_with_sequence_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  (gpointer)&rx_data,
				  error))
		return FALSE;
	/* no child device to respond properly */
	if (rx_data.rxbuf[5] == 0xFE && rx_data.rxbuf[6] == 0xF4) {
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
fu_jabra_gnp_device_read_child_battery_level(FuJabraGnpDevice *self, GError **error)
{
	FuJabraGnpTxData tx_data = {
	    .txbuf =
		{
		    FU_JABRA_GNP_IFACE,
		    self->address,
		    0x00,
		    self->sequence_number,
		    0x46,
		    0x12,
		    0x02,
		},
	    .timeout = FU_JABRA_GNP_STANDARD_SEND_TIMEOUT,
	};
	FuJabraGnpRxData rx_data = {
	    .rxbuf = {0x00},
	    .timeout = FU_JABRA_GNP_STANDARD_RECEIVE_TIMEOUT,
	};
	guint8 battery_level = 0;
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_jabra_gnp_device_tx_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  (gpointer)&tx_data,
				  error))
		return FALSE;
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_jabra_gnp_device_rx_with_sequence_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  (gpointer)&rx_data,
				  error))
		return FALSE;
	if (!fu_memread_uint8_safe(rx_data.rxbuf, FU_JABRA_GNP_BUF_SIZE, 8, &battery_level, error))
		return FALSE;
	if (battery_level == 0x00) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "battery level was 0");
		return FALSE;
	}
	fu_device_set_battery_level(FU_DEVICE(self), battery_level);
	fu_device_set_battery_threshold(FU_DEVICE(self), 30);
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
				  (gpointer)&tx_data,
				  error))
		return FALSE;
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_jabra_gnp_device_rx_with_sequence_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  (gpointer)&rx_data,
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
				  (gpointer)&tx_data,
				  error))
		return FALSE;
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_jabra_gnp_device_rx_with_sequence_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  (gpointer)&rx_data,
				  error))
		return FALSE;
	version = fu_memstrsafe(rx_data.rxbuf,
				sizeof(rx_data.rxbuf),
				0x8,
				sizeof(rx_data.rxbuf) - 8,
				error);
	if (version == NULL)
		return FALSE;
	fu_device_set_version(FU_DEVICE(self), version);
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
				  (gpointer)&tx_data,
				  error))
		return FALSE;
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_jabra_gnp_device_rx_with_sequence_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  (gpointer)&rx_data,
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
				  (gpointer)&tx_data,
				  error))
		return FALSE;
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_jabra_gnp_device_rx_with_sequence_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  (gpointer)&rx_data,
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
				  (gpointer)&rx_data,
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
				  (gpointer)&tx_data,
				  error))
		return FALSE;
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_jabra_gnp_device_rx_with_sequence_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  (gpointer)&rx_data,
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
				guint32 bufsz,
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
				    (gpointer)&tx_data,
				    error);
}

static gboolean
fu_jabra_gnp_device_write_chunks(FuJabraGnpDevice *self,
				 FuChunkArray *chunks,
				 FuProgress *progress,
				 GError **error)
{
	gboolean failed_chunk = FALSE;

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
	for (gint chunk_number = 0; (guint)chunk_number < fu_chunk_array_length(chunks);
	     chunk_number++) {
		g_autoptr(FuChunk) chk = fu_chunk_array_index(chunks, chunk_number);
		if (!fu_jabra_gnp_device_write_chunk(self,
						     chunk_number,
						     fu_chunk_get_data(chk),
						     fu_chunk_get_data_sz(chk),
						     error))
			return FALSE;
		if (((chunk_number % FU_JABRA_GNP_PRELOAD_COUNT) == 0) ||
		    (guint)chunk_number == fu_chunk_array_length(chunks) - 1) {
			if (!fu_device_retry_full(FU_DEVICE(self),
						  fu_jabra_gnp_device_rx_cb,
						  FU_JABRA_GNP_MAX_RETRIES,
						  FU_JABRA_GNP_RETRY_DELAY,
						  (gpointer)&rx_data,
						  error))
				return FALSE;
			if (rx_data.rxbuf[5] != match_buf[5] || rx_data.rxbuf[6] != match_buf[6]) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INTERNAL,
					    "internal error, buf did not match");
				return FALSE;
			}
			if (fu_memread_uint16(rx_data.rxbuf + 7, G_LITTLE_ENDIAN) != chunk_number) {
				chunk_number--;
				failed_chunk = TRUE;
			} else
				failed_chunk = FALSE;
		}
		if (!failed_chunk)
			fu_progress_step_done(progress);
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
	    .timeout = FU_JABRA_GNP_LONG_RECEIVE_TIMEOUT,
	};
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_jabra_gnp_device_rx_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  (gpointer)&rx_data,
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
				  (gpointer)&tx_data,
				  error))
		return FALSE;
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_jabra_gnp_device_rx_with_sequence_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  (gpointer)&rx_data,
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
				  (gpointer)&tx_data,
				  error))
		return FALSE;
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_jabra_gnp_device_rx_with_sequence_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  (gpointer)&rx_data,
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
				     GBytes *fw,
				     FwupdInstallFlags flags,
				     GError **error)
{
	FuJabraGnpDevice *self = FU_JABRA_GNP_DEVICE(device);
	g_autoptr(FuFirmware) firmware = fu_jabra_gnp_firmware_new();

	/* unzip and get images */
	if (!fu_firmware_parse(firmware, fw, flags, error))
		return NULL;
	if (fu_jabra_gnp_firmware_get_dfu_pid(FU_JABRA_GNP_FIRMWARE(firmware)) != self->dfu_pid) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "wrong DFU PID, got 0x%x, expected 0x%x",
			    fu_jabra_gnp_firmware_get_dfu_pid(FU_JABRA_GNP_FIRMWARE(firmware)),
			    self->dfu_pid);
		return NULL;
	}
	return g_steal_pointer(&firmware);
}

static gboolean
fu_jabra_gnp_device_probe(FuDevice *device, GError **error)
{
	g_autoptr(GError) error_local = NULL;
	FuJabraGnpDevice *self = FU_JABRA_GNP_DEVICE(device);

	/* already set by parent */
	if (self->address == FU_JABRA_GNP_ADDRESS_OTA_CHILD)
		return TRUE;

	self->iface_hid =
	    _g_usb_device_get_interface_for_class(fu_usb_device_get_dev(FU_USB_DEVICE(self)),
						  G_USB_DEVICE_CLASS_HID,
						  &error_local);
	if (self->iface_hid == 0xFF) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "cannot find HID interface: %s",
			    error_local->message);
		return FALSE;
	}
	fu_usb_device_add_interface(FU_USB_DEVICE(self), self->iface_hid);
	return TRUE;
}

static gboolean
fu_jabra_gnp_device_add_child(FuDevice *device, guint dfu_pid, GError **error)
{
	FuJabraGnpDevice *self = FU_JABRA_GNP_DEVICE(device);
	g_autoptr(FuJabraGnpDevice) child = NULL;

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

	child = g_object_new(FU_TYPE_JABRA_GNP_DEVICE, "parent", FU_DEVICE(self), NULL);
	child->iface_hid = self->iface_hid;
	child->sequence_number = 0;
	child->address = FU_JABRA_GNP_ADDRESS_OTA_CHILD;
	child->dfu_pid = dfu_pid;

	/* prohibit to close parent's communication descriptor */
	fu_device_add_internal_flag(FU_DEVICE(child), FU_DEVICE_INTERNAL_FLAG_USE_PARENT_FOR_OPEN);

	fu_device_set_physical_id(FU_DEVICE(child), fu_device_get_physical_id(FU_DEVICE(self)));
	fu_device_set_logical_id(FU_DEVICE(child), "ota_device");

	if (!fu_device_setup(FU_DEVICE(child), error)) {
		g_prefix_error(error, "failed to setup child device: ");
		return FALSE;
	}

	fu_device_add_instance_u16(FU_DEVICE(child),
				   "VID",
				   fu_usb_device_get_vid(FU_USB_DEVICE(self)));
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
	if (self->address == FU_JABRA_GNP_ADDRESS_OTA_CHILD) {
		if (!fu_jabra_gnp_device_read_child_battery_level(self, error))
			return FALSE;
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
	g_autoptr(GBytes) blob = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 1, "write-partition");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 1, "start");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 5, "flash-erase-done");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 91, "write-chunks");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 1, "read-verify-status");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 1, "write-version");

	/* write partition */
	blob = fu_firmware_get_bytes(img, error);
	if (blob == NULL)
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
	chunks = fu_chunk_array_new_from_bytes(blob, 0x00, chunk_size);
	if (!fu_jabra_gnp_device_write_crc(self,
					   fu_jabra_gnp_image_get_crc32(FU_JABRA_GNP_IMAGE(img)),
					   fu_chunk_array_length(chunks),
					   FU_JABRA_GNP_PRELOAD_COUNT,
					   error))
		return FALSE;
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
fu_jabra_gnp_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuJabraGnpDevice *self = FU_JABRA_GNP_DEVICE(device);
	/* ota device needs some time to restart and reconnect */
	if (self->address == FU_JABRA_GNP_ADDRESS_OTA_CHILD) {
		fu_device_sleep_full(FU_DEVICE(self), 45000, progress);
		fu_device_set_remove_delay(FU_DEVICE(self), 10000);
		fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	}
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
		if (!fu_strtoull(value, &val, 0x0, G_MAXUINT8, error))
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
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_ADD_COUNTERPART_GUIDS);
	fu_device_add_protocol(FU_DEVICE(self), "com.jabra.gnp");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_JABRA_GNP_FIRMWARE);
}

static void
fu_jabra_gnp_device_class_init(FuJabraGnpDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->to_string = fu_jabra_gnp_device_to_string;
	klass_device->prepare_firmware = fu_jabra_gnp_device_prepare_firmware;
	klass_device->probe = fu_jabra_gnp_device_probe;
	klass_device->setup = fu_jabra_gnp_device_setup;
	klass_device->write_firmware = fu_jabra_gnp_device_write_firmware;
	klass_device->attach = fu_jabra_gnp_device_attach;
	klass_device->set_quirk_kv = fu_jabra_gnp_device_set_quirk_kv;
	klass_device->set_progress = fu_jabra_gnp_device_set_progress;
}
