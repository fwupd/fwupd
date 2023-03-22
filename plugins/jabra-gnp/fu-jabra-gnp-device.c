/*
 * Copyright (C) 2023 GN Audio
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-jabra-gnp-device.h"
#include "fu-jabra-gnp-firmware.h"
#include "fu-jabra-gnp-image.h"

#define FU_JABRA_GNP_BUF_SIZE			63
#define FU_JABRA_GNP_MAX_RETRIES		3
#define FU_JABRA_GNP_RETRY_DELAY		100   /* ms */
#define FU_JABRA_GNP_STANDARD_SEND_TIMEOUT	3000  /* ms */
#define FU_JABRA_GNP_STANDARD_RECEIVE_TIMEOUT	1000  /* ms */
#define FU_JABRA_GNP_LONG_RECEIVE_TIMEOUT	30000 /* ms */
#define FU_JABRA_GNP_EXTRA_LONG_RECEIVE_TIMEOUT 60000 /* ms */

#define FU_JABRA_GNP_IFACE 0x05

struct _FuJabraGnpDevice {
	FuUsbDevice parent_instance;
	guint8 iface_hid;
	guint8 sequence_number;
	guint dfu_pid;
};

typedef struct {
	guint8 *txbuf;
	const guint timeout;
} FuJabraGnpTxData;

typedef struct {
	guint8 *rxbuf;
	const guint timeout;
} FuJabraGnpRxData;

G_DEFINE_TYPE(FuJabraGnpDevice, fu_jabra_gnp_device, FU_TYPE_USB_DEVICE)

static void
fu_jabra_gnp_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuJabraGnpDevice *self = FU_JABRA_GNP_DEVICE(device);
	fu_string_append_kx(str, idt, "IfaceHid", self->iface_hid);
	fu_string_append_kx(str, idt, "SequenceNumber", self->sequence_number);
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
	if (!g_usb_device_control_transfer(fu_usb_device_get_dev(FU_USB_DEVICE(self)),
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
	const guint8 match_buf[FU_JABRA_GNP_BUF_SIZE] =
	    {FU_JABRA_GNP_IFACE, 0x08, 0x00, 0x00, 0x0A, 0x12, 0x02};
	FuJabraGnpDevice *self = FU_JABRA_GNP_DEVICE(device);
	FuJabraGnpRxData *rx_data = (FuJabraGnpRxData *)user_data;
	if (!g_usb_device_interrupt_transfer(fu_usb_device_get_dev(FU_USB_DEVICE(self)),
					     0x81,
					     rx_data->rxbuf,
					     FU_JABRA_GNP_BUF_SIZE,
					     NULL,
					     rx_data->timeout,
					     NULL, /* cancellable */
					     error)) {
		g_prefix_error(error, "failed to read from device: ");
		return FALSE;
	} else {
		if (rx_data->rxbuf[5] == match_buf[5] && rx_data->rxbuf[6] == match_buf[6]) {
			/* battery report, ignore and rx again */
			if (!g_usb_device_interrupt_transfer(
				fu_usb_device_get_dev(FU_USB_DEVICE(self)),
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
	}

	return TRUE;
}

static gboolean
fu_jabra_gnp_device_rx_with_sequence_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuJabraGnpDevice *self = FU_JABRA_GNP_DEVICE(device);
	FuJabraGnpRxData *rx_data = (FuJabraGnpRxData *)user_data;

	if (!fu_jabra_gnp_device_rx_cb(device, user_data, error))
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
fu_jabra_gnp_device_read_dfu_pid(FuJabraGnpDevice *self, GError **error)
{
	guint8 txbuf[FU_JABRA_GNP_BUF_SIZE] =
	    {FU_JABRA_GNP_IFACE, 0x08, 0x00, self->sequence_number, 0x46, 0x02, 0x13};
	guint8 rxbuf[FU_JABRA_GNP_BUF_SIZE] = {0x00};
	FuJabraGnpTxData tx_data = {.txbuf = txbuf, .timeout = FU_JABRA_GNP_STANDARD_SEND_TIMEOUT};
	FuJabraGnpRxData rx_data = {.rxbuf = rxbuf,
				    .timeout = FU_JABRA_GNP_STANDARD_RECEIVE_TIMEOUT};

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
	guint8 txbuf[FU_JABRA_GNP_BUF_SIZE] =
	    {FU_JABRA_GNP_IFACE, 0x08, 0x00, self->sequence_number, 0x46, 0x02, 0x03};
	guint8 rxbuf[FU_JABRA_GNP_BUF_SIZE] = {0x00};
	FuJabraGnpTxData tx_data = {.txbuf = txbuf, .timeout = FU_JABRA_GNP_STANDARD_SEND_TIMEOUT};
	FuJabraGnpRxData rx_data = {.rxbuf = rxbuf,
				    .timeout = FU_JABRA_GNP_STANDARD_RECEIVE_TIMEOUT};
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
	version = fu_strsafe((const gchar *)rx_data.rxbuf + 8, FU_JABRA_GNP_BUF_SIZE - 8);
	fu_device_set_version(FU_DEVICE(self), version);
	return TRUE;
}

static gboolean
fu_jabra_gnp_device_write_partition(FuJabraGnpDevice *self, guint8 part, GError **error)
{
	guint8 txbuf[FU_JABRA_GNP_BUF_SIZE] =
	    {FU_JABRA_GNP_IFACE, 0x08, 0x00, self->sequence_number, 0x87, 0x0F, 0x2D, part};
	guint8 rxbuf[FU_JABRA_GNP_BUF_SIZE] = {0x00};
	FuJabraGnpTxData tx_data = {.txbuf = txbuf, .timeout = FU_JABRA_GNP_STANDARD_SEND_TIMEOUT};
	FuJabraGnpRxData rx_data = {.rxbuf = rxbuf,
				    .timeout = FU_JABRA_GNP_STANDARD_RECEIVE_TIMEOUT};
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
	guint8 txbuf[FU_JABRA_GNP_BUF_SIZE] =
	    {FU_JABRA_GNP_IFACE, 0x08, 0x00, self->sequence_number, 0x86, 0x0F, 0x17};
	guint8 rxbuf[FU_JABRA_GNP_BUF_SIZE] = {0x00};
	FuJabraGnpTxData tx_data = {.txbuf = txbuf, .timeout = FU_JABRA_GNP_STANDARD_SEND_TIMEOUT};
	FuJabraGnpRxData rx_data = {.rxbuf = rxbuf,
				    .timeout = FU_JABRA_GNP_STANDARD_RECEIVE_TIMEOUT};
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
	guint8 rxbuf[FU_JABRA_GNP_BUF_SIZE] = {0x00};
	const guint8 match_buf[FU_JABRA_GNP_BUF_SIZE] =
	    {FU_JABRA_GNP_IFACE, 0x08, 0x00, 0x00, 0x06, 0x0F, 0x18};
	FuJabraGnpRxData rx_data = {.rxbuf = rxbuf,
				    .timeout = FU_JABRA_GNP_EXTRA_LONG_RECEIVE_TIMEOUT};
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
	guint8 txbuf[FU_JABRA_GNP_BUF_SIZE] =
	    {FU_JABRA_GNP_IFACE, 0x08, 0x00, self->sequence_number, 0x8E, 0x0F, 0x19};
	guint8 rxbuf[FU_JABRA_GNP_BUF_SIZE] = {0x00};
	FuJabraGnpTxData tx_data = {.txbuf = txbuf, .timeout = FU_JABRA_GNP_STANDARD_SEND_TIMEOUT};
	FuJabraGnpRxData rx_data = {.rxbuf = rxbuf,
				    .timeout = FU_JABRA_GNP_STANDARD_RECEIVE_TIMEOUT};

	fu_memwrite_uint32(txbuf + 7, crc, G_LITTLE_ENDIAN);
	fu_memwrite_uint16(txbuf + 11, total_chunks, G_LITTLE_ENDIAN);
	fu_memwrite_uint16(txbuf + 13, preload_count, G_LITTLE_ENDIAN);

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
	guint8 txbuf[FU_JABRA_GNP_BUF_SIZE] =
	    {FU_JABRA_GNP_IFACE, 0x08, 0x00, 0x00, write_length, 0x0F, 0x1A};
	FuJabraGnpTxData tx_data = {.txbuf = txbuf, .timeout = FU_JABRA_GNP_STANDARD_SEND_TIMEOUT};
	fu_memwrite_uint16(txbuf + 7, chunk_number, G_LITTLE_ENDIAN);
	fu_memwrite_uint16(txbuf + 9, bufsz, G_LITTLE_ENDIAN);
	if (!fu_memcpy_safe(txbuf, sizeof(txbuf), 11, buf, bufsz, 0x0, bufsz, error))
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
				 GPtrArray *chunks,
				 FuProgress *progress,
				 GError **error)
{
	gboolean failed_chunk = FALSE;
	guint32 preload_count = 100;
	guint8 rxbuf[FU_JABRA_GNP_BUF_SIZE] = {0x00};
	const guint8 match_buf[FU_JABRA_GNP_BUF_SIZE] =
	    {FU_JABRA_GNP_IFACE, 0x08, 0x00, 0x00, 0x06, 0x0F, 0x1B};
	FuJabraGnpRxData rx_data = {.rxbuf = rxbuf, .timeout = FU_JABRA_GNP_LONG_RECEIVE_TIMEOUT};

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, chunks->len);
	for (gint chunk_number = 0; (guint)chunk_number < chunks->len; chunk_number++) {
		FuChunk *chk = g_ptr_array_index(chunks, chunk_number);
		if (!fu_jabra_gnp_device_write_chunk(self,
						     chunk_number,
						     fu_chunk_get_data(chk),
						     fu_chunk_get_data_sz(chk),
						     error))
			return FALSE;
		if (((chunk_number % preload_count) == 0) ||
		    (guint)chunk_number == chunks->len - 1) {
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
			if (fu_memread_uint16(rxbuf + 7, G_LITTLE_ENDIAN) != chunk_number) {
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
	guint8 rxbuf[FU_JABRA_GNP_BUF_SIZE] = {0x00};
	const guint8 match_buf[FU_JABRA_GNP_BUF_SIZE] =
	    {FU_JABRA_GNP_IFACE, 0x08, 0x00, 0x00, 0x06, 0x0F, 0x1C};
	FuJabraGnpRxData rx_data = {.rxbuf = rxbuf, .timeout = FU_JABRA_GNP_LONG_RECEIVE_TIMEOUT};
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
	guint8 txbuf[FU_JABRA_GNP_BUF_SIZE] = {FU_JABRA_GNP_IFACE,
					       0x08,
					       0x00,
					       self->sequence_number,
					       0x89,
					       0x0F,
					       0x1E,
					       version_data->major,
					       version_data->minor,
					       version_data->micro};
	guint8 rxbuf[FU_JABRA_GNP_BUF_SIZE] = {0x00};
	FuJabraGnpTxData tx_data = {.txbuf = txbuf, .timeout = FU_JABRA_GNP_STANDARD_SEND_TIMEOUT};
	FuJabraGnpRxData rx_data = {.rxbuf = rxbuf,
				    .timeout = FU_JABRA_GNP_STANDARD_RECEIVE_TIMEOUT};
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
	guint8 txbuf[FU_JABRA_GNP_BUF_SIZE] =
	    {FU_JABRA_GNP_IFACE, 0x08, 0x00, self->sequence_number, 0x86, 0x0F, 0x1D};
	guint8 rxbuf[FU_JABRA_GNP_BUF_SIZE] = {0x00};
	FuJabraGnpTxData tx_data = {.txbuf = txbuf, .timeout = FU_JABRA_GNP_STANDARD_SEND_TIMEOUT};
	FuJabraGnpRxData rx_data = {.rxbuf = rxbuf,
				    .timeout = FU_JABRA_GNP_STANDARD_RECEIVE_TIMEOUT};
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
fu_jabra_gnp_device_setup(FuDevice *device, GError **error)
{
	FuJabraGnpDevice *self = FU_JABRA_GNP_DEVICE(device);
	if (!fu_jabra_gnp_device_read_version(self, error))
		return FALSE;
	if (!fu_jabra_gnp_device_read_dfu_pid(self, error))
		return FALSE;
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
	g_autoptr(GPtrArray) chunks = NULL;
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
	chunks = fu_chunk_array_new_from_bytes(blob, 0x00, 0x00, chunk_size);
	if (!fu_jabra_gnp_device_write_crc(self,
					   fu_jabra_gnp_image_get_crc32(FU_JABRA_GNP_IMAGE(img)),
					   chunks->len,
					   100,
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

static void
fu_colorhug_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 5, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 85, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 5, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 5, "reload");
}

static void
fu_jabra_gnp_device_init(FuJabraGnpDevice *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_DUAL_IMAGE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SELF_RECOVERY);
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
	klass_device->set_progress = fu_colorhug_device_set_progress;
}
