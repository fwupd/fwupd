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

struct _FuJabraGnpChildDevice {
	FuDevice parent_instance;
	guint8 fwu_protocol;
	guint8 sequence_number;
	guint8 address;
	guint16 dfu_pid;
};

G_DEFINE_TYPE(FuJabraGnpChildDevice, fu_jabra_gnp_child_device, FU_TYPE_DEVICE)

static void
fu_jabra_gnp_child_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuJabraGnpChildDevice *self = FU_JABRA_GNP_CHILD_DEVICE(device);
	fwupd_codec_string_append_hex(str, idt, "FwuProtocol", self->fwu_protocol);
	fwupd_codec_string_append_hex(str, idt, "SequenceNumber", self->sequence_number);
	fwupd_codec_string_append_hex(str, idt, "Address", self->address);
	fwupd_codec_string_append_hex(str, idt, "DfuPid", self->dfu_pid);
}

void
fu_jabra_gnp_child_device_set_dfu_pid(FuJabraGnpChildDevice *self, guint16 dfu_pid)
{
	g_return_if_fail(FU_IS_JABRA_GNP_CHILD_DEVICE(self));
	self->dfu_pid = dfu_pid;
}

static gboolean
fu_jabra_gnp_child_device_tx_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuJabraGnpTxData *tx_data = (FuJabraGnpTxData *)user_data;
	FuJabraGnpDevice *parent = FU_JABRA_GNP_DEVICE(fu_device_get_parent(device));
	if (!fu_usb_device_control_transfer(FU_USB_DEVICE(parent),
					    FU_USB_DIRECTION_HOST_TO_DEVICE,
					    FU_USB_REQUEST_TYPE_CLASS,
					    FU_USB_RECIPIENT_INTERFACE,
					    0x09,
					    0x0200 | FU_JABRA_GNP_IFACE,
					    fu_jabra_gnp_device_get_iface_hid(parent),
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
fu_jabra_gnp_child_device_rx_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuJabraGnpChildDevice *self = FU_JABRA_GNP_CHILD_DEVICE(device);
	const guint8 match_buf[FU_JABRA_GNP_BUF_SIZE] = {
	    FU_JABRA_GNP_IFACE,
	    0x00,
	    self->address,
	    0x00,
	    0x0A,
	    0x12,
	    0x02,
	};
	const guint8 empty_buf[FU_JABRA_GNP_BUF_SIZE] = {0x00};
	FuJabraGnpRxData *rx_data = (FuJabraGnpRxData *)user_data;
	FuJabraGnpDevice *parent = FU_JABRA_GNP_DEVICE(fu_device_get_parent(device));

	if (!fu_usb_device_interrupt_transfer(FU_USB_DEVICE(parent),
					      fu_jabra_gnp_device_get_epin(parent),
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
		/* battery report, ignpre and rx again */
		if (!fu_usb_device_interrupt_transfer(FU_USB_DEVICE(parent),
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
		g_prefix_error(error, "error reading from device: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_jabra_gnp_child_device_rx_with_sequence_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuJabraGnpChildDevice *self = FU_JABRA_GNP_CHILD_DEVICE(device);
	FuJabraGnpRxData *rx_data = (FuJabraGnpRxData *)user_data;

	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_jabra_gnp_child_device_rx_cb,
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
fu_jabra_gnp_child_device_read_name(FuJabraGnpChildDevice *self, GError **error)
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
				  fu_jabra_gnp_child_device_tx_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  &tx_data,
				  error))
		return FALSE;
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_jabra_gnp_child_device_rx_with_sequence_cb,
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
fu_jabra_gnp_child_device_read_battery_level(FuJabraGnpChildDevice *self, GError **error)
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
				  fu_jabra_gnp_child_device_tx_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  &tx_data,
				  error))
		return FALSE;
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_jabra_gnp_child_device_rx_with_sequence_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  &rx_data,
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
fu_jabra_gnp_child_device_read_dfu_pid(FuJabraGnpChildDevice *self, GError **error)
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
				  fu_jabra_gnp_child_device_tx_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  &tx_data,
				  error))
		return FALSE;
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_jabra_gnp_child_device_rx_with_sequence_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  &rx_data,
				  error))
		return FALSE;
	self->dfu_pid = fu_memread_uint16(rx_data.rxbuf + 7, G_LITTLE_ENDIAN);
	return TRUE;
}

static gboolean
fu_jabra_gnp_child_device_read_version(FuJabraGnpChildDevice *self, GError **error)
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
				  fu_jabra_gnp_child_device_tx_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  &tx_data,
				  error))
		return FALSE;
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_jabra_gnp_child_device_rx_with_sequence_cb,
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

	/* some devices append an extra '.' to the version, which can confuse fwupd's formats, so
	 * remove it */
	if (g_str_has_suffix(version, "."))
		version[strlen(version) - 1] = '\0';

	fu_device_set_version(FU_DEVICE(self), version);
	return TRUE;
}

static gboolean
fu_jabra_gnp_child_device_read_fwu_protocol(FuJabraGnpChildDevice *self, GError **error)
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
				  fu_jabra_gnp_child_device_tx_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  &tx_data,
				  error))
		return FALSE;
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_jabra_gnp_child_device_rx_with_sequence_cb,
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
fu_jabra_gnp_child_device_write_partition(FuJabraGnpChildDevice *self, guint8 part, GError **error)
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
				  fu_jabra_gnp_child_device_tx_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  &tx_data,
				  error))
		return FALSE;
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_jabra_gnp_child_device_rx_with_sequence_cb,
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
fu_jabra_gnp_child_device_start(FuJabraGnpChildDevice *self, GError **error)
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
				  fu_jabra_gnp_child_device_tx_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  &tx_data,
				  error))
		return FALSE;
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_jabra_gnp_child_device_rx_with_sequence_cb,
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
fu_jabra_gnp_child_device_flash_erase_done(FuJabraGnpChildDevice *self, GError **error)
{
	const guint8 match_buf[FU_JABRA_GNP_BUF_SIZE] = {
	    FU_JABRA_GNP_IFACE,
	    0x00,
	    self->address,
	    0x00,
	    0x06,
	    0x0F,
	    0x18,
	};
	FuJabraGnpRxData rx_data = {
	    .rxbuf = {0x00},
	    .timeout = FU_JABRA_GNP_EXTRA_LONG_RECEIVE_TIMEOUT,
	};

	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_jabra_gnp_child_device_rx_cb,
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
fu_jabra_gnp_child_device_write_crc(FuJabraGnpChildDevice *self,
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
				  fu_jabra_gnp_child_device_tx_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  &tx_data,
				  error))
		return FALSE;
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_jabra_gnp_child_device_rx_with_sequence_cb,
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
fu_jabra_gnp_child_device_write_extended_crc(FuJabraGnpChildDevice *self,
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
		    0x90,
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
	fu_memwrite_uint16(tx_data.txbuf + 11, 0x00, G_LITTLE_ENDIAN);
	fu_memwrite_uint16(tx_data.txbuf + 13, preload_count, G_LITTLE_ENDIAN);
	fu_memwrite_uint32(tx_data.txbuf + 15, total_chunks, G_LITTLE_ENDIAN);

	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_jabra_gnp_child_device_tx_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  &tx_data,
				  error))
		return FALSE;
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_jabra_gnp_child_device_rx_with_sequence_cb,
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
fu_jabra_gnp_child_device_write_chunk(FuJabraGnpChildDevice *self,
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
				    fu_jabra_gnp_child_device_tx_cb,
				    FU_JABRA_GNP_MAX_RETRIES,
				    FU_JABRA_GNP_RETRY_DELAY,
				    &tx_data,
				    error);
}

static gboolean
fu_jabra_gnp_child_device_write_chunks(FuJabraGnpChildDevice *self,
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
		g_autoptr(FuChunk) chk = NULL;

		/* prepare chunk */
		chk = fu_chunk_array_index(chunks, chunk_number, error);
		if (chk == NULL)
			return FALSE;
		if (!fu_jabra_gnp_child_device_write_chunk(self,
							   chunk_number,
							   fu_chunk_get_data(chk),
							   fu_chunk_get_data_sz(chk),
							   error))
			return FALSE;
		if (((chunk_number % FU_JABRA_GNP_PRELOAD_COUNT) == 0) ||
		    (guint)chunk_number == fu_chunk_array_length(chunks) - 1) {
			if (!fu_device_retry_full(FU_DEVICE(self),
						  fu_jabra_gnp_child_device_rx_cb,
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
fu_jabra_gnp_child_device_read_verify_status(FuJabraGnpChildDevice *self, GError **error)
{
	const guint8 match_buf[FU_JABRA_GNP_BUF_SIZE] = {
	    FU_JABRA_GNP_IFACE,
	    0x00,
	    self->address,
	    0x00,
	    0x06,
	    0x0F,
	    0x1C,
	};
	FuJabraGnpRxData rx_data = {
	    .rxbuf = {0x00},
	    .timeout = FU_JABRA_GNP_LONG_RECEIVE_TIMEOUT,
	};

	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_jabra_gnp_child_device_rx_cb,
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
fu_jabra_gnp_child_device_write_version(FuJabraGnpChildDevice *self,
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
				  fu_jabra_gnp_child_device_tx_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  &tx_data,
				  error))
		return FALSE;
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_jabra_gnp_child_device_rx_with_sequence_cb,
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
fu_jabra_gnp_child_device_write_dfu_from_squif(FuJabraGnpChildDevice *self, GError **error)
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
				  fu_jabra_gnp_child_device_tx_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  &tx_data,
				  error))
		return FALSE;
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_jabra_gnp_child_device_rx_with_sequence_cb,
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
fu_jabra_gnp_child_device_prepare_firmware(FuDevice *device,
					   GInputStream *stream,
					   FuProgress *progress,
					   FuFirmwareParseFlags flags,
					   GError **error)
{
	FuJabraGnpChildDevice *self = FU_JABRA_GNP_CHILD_DEVICE(device);
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
fu_jabra_gnp_child_device_setup(FuDevice *device, GError **error)
{
	FuJabraGnpChildDevice *self = FU_JABRA_GNP_CHILD_DEVICE(device);

	if (!fu_jabra_gnp_child_device_read_name(self, error))
		return FALSE;
	if (!fu_jabra_gnp_child_device_read_version(self, error))
		return FALSE;
	if (!fu_jabra_gnp_child_device_read_dfu_pid(self, error))
		return FALSE;
	if (!fu_jabra_gnp_child_device_read_battery_level(self, error))
		return FALSE;
	return TRUE;
}

static gboolean
fu_jabra_gnp_child_device_write_image(FuJabraGnpChildDevice *self,
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
	if (!fu_jabra_gnp_child_device_write_partition(self, fu_firmware_get_idx(img), error))
		return FALSE;
	fu_progress_step_done(progress);

	/* start erasing */
	if (!fu_jabra_gnp_child_device_start(self, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* poll for erase done */
	if (!fu_jabra_gnp_child_device_flash_erase_done(self, error))
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
		if (!fu_jabra_gnp_child_device_write_crc(
			self,
			fu_jabra_gnp_image_get_crc32(FU_JABRA_GNP_IMAGE(img)),
			fu_chunk_array_length(chunks),
			FU_JABRA_GNP_PRELOAD_COUNT,
			error))
			return FALSE;
	} else {
		/* self->fwu_protocol == FU_JABRA_GNP_PROTOCOL_EXTENDED_OTA*/
		if (!fu_jabra_gnp_child_device_write_extended_crc(
			self,
			fu_jabra_gnp_image_get_crc32(FU_JABRA_GNP_IMAGE(img)),
			fu_chunk_array_length(chunks),
			FU_JABRA_GNP_PRELOAD_COUNT,
			error))
			return FALSE;
	}
	if (!fu_jabra_gnp_child_device_write_chunks(self,
						    chunks,
						    fu_progress_get_child(progress),
						    error))
		return FALSE;
	fu_progress_step_done(progress);

	/* verify */
	if (!fu_jabra_gnp_child_device_read_verify_status(self, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* write version */
	if (!fu_jabra_gnp_child_device_write_version(
		self,
		fu_jabra_gnp_firmware_get_version_data(FU_JABRA_GNP_FIRMWARE(firmware)),
		error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static gboolean
fu_jabra_gnp_child_device_write_firmware(FuDevice *device,
					 FuFirmware *firmware,
					 FuProgress *progress,
					 FwupdInstallFlags flags,
					 GError **error)
{
	FuJabraGnpChildDevice *self = FU_JABRA_GNP_CHILD_DEVICE(device);
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
	if (!fu_jabra_gnp_child_device_read_fwu_protocol(self, error))
		return FALSE;
	for (guint i = 0; i < imgs->len; i++) {
		FuFirmware *img = g_ptr_array_index(imgs, i);
		if (!fu_jabra_gnp_child_device_write_image(self,
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
	return fu_jabra_gnp_child_device_write_dfu_from_squif(self, error);
}

static gboolean
fu_jabra_gnp_child_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuJabraGnpChildDevice *self = FU_JABRA_GNP_CHILD_DEVICE(device);
	fu_device_sleep_full(FU_DEVICE(self), 45000, progress);
	return TRUE;
}

static gboolean
fu_jabra_gnp_child_device_set_quirk_kv(FuDevice *device,
				       const gchar *key,
				       const gchar *value,
				       GError **error)
{
	FuJabraGnpChildDevice *self = FU_JABRA_GNP_CHILD_DEVICE(device);

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
fu_jabra_gnp_child_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 5, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 75, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 5, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 15, "reload");
}

static void
fu_jabra_gnp_child_device_init(FuJabraGnpChildDevice *self)
{
	self->address = FU_JABRA_GNP_ADDRESS_OTA_CHILD;
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_DUAL_IMAGE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SELF_RECOVERY);
	/* prohibit to close parent's communication descriptor */
	fu_device_set_logical_id(FU_DEVICE(self), "ota_device");
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_USE_PARENT_FOR_OPEN);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_ADD_COUNTERPART_GUIDS);
	fu_device_add_protocol(FU_DEVICE(self), "com.jabra.gnp");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_JABRA_GNP_FIRMWARE);
	fu_device_set_remove_delay(FU_DEVICE(self), 10000);
}

static void
fu_jabra_gnp_child_device_class_init(FuJabraGnpChildDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->to_string = fu_jabra_gnp_child_device_to_string;
	device_class->prepare_firmware = fu_jabra_gnp_child_device_prepare_firmware;
	device_class->setup = fu_jabra_gnp_child_device_setup;
	device_class->write_firmware = fu_jabra_gnp_child_device_write_firmware;
	device_class->attach = fu_jabra_gnp_child_device_attach;
	device_class->set_quirk_kv = fu_jabra_gnp_child_device_set_quirk_kv;
	device_class->set_progress = fu_jabra_gnp_child_device_set_progress;
}
