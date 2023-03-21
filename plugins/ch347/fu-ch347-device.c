/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-ch347-cfi-device.h"
#include "fu-ch347-device.h"

struct _FuCh347Device {
	FuUsbDevice parent_instance;
	guint8 divisor;
};

G_DEFINE_TYPE(FuCh347Device, fu_ch347_device, FU_TYPE_USB_DEVICE)

#define FU_CH347_USB_TIMEOUT 1000

#define FU_CH347_CMD_SPI_SET_CFG 0xC0
#define FU_CH347_CMD_SPI_CS_CTRL 0xC1
#define FU_CH347_CMD_SPI_OUT_IN	 0xC2
#define FU_CH347_CMD_SPI_IN	 0xC3
#define FU_CH347_CMD_SPI_OUT	 0xC4
#define FU_CH347_CMD_SPI_GET_CFG 0xCA

#define FU_CH347_CS_ASSERT   0x00
#define FU_CH347_CS_DEASSERT 0x40
#define FU_CH347_CS_CHANGE   0x80
#define FU_CH347_CS_IGNORE   0x00

#define FU_CH347_EP_OUT 0x06
#define FU_CH347_EP_IN	0x86

#define FU_CH347_MODE1_IFACE 0x2
#define FU_CH347_MODE2_IFACE 0x1

#define FU_CH347_PACKET_SIZE  510
#define FU_CH347_PAYLOAD_SIZE (FU_CH347_PACKET_SIZE - 3)

static void
fu_ch347_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuCh347Device *self = FU_CH347_DEVICE(device);

	/* FuUsbDevice->to_string */
	FU_DEVICE_CLASS(fu_ch347_device_parent_class)->to_string(device, idt, str);

	fu_string_append_kx(str, idt, "Divisor", self->divisor);
}

static gboolean
fu_ch347_device_write(FuCh347Device *self,
		      guint8 cmd,
		      const guint8 *buf,
		      gsize bufsz,
		      GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(self));
	gsize actual_length = 0;
	g_autoptr(GByteArray) cmdbuf = g_byte_array_new();

	/* pack */
	fu_byte_array_append_uint8(cmdbuf, cmd);
	fu_byte_array_append_uint16(cmdbuf, bufsz, G_LITTLE_ENDIAN);
	if (bufsz > 0)
		g_byte_array_append(cmdbuf, buf, bufsz);

	/* debug */
	fu_dump_raw(G_LOG_DOMAIN, "write", cmdbuf->data, cmdbuf->len);
	if (!g_usb_device_bulk_transfer(usb_device,
					FU_CH347_EP_OUT,
					cmdbuf->data,
					cmdbuf->len,
					&actual_length,
					FU_CH347_USB_TIMEOUT,
					NULL,
					error)) {
		g_prefix_error(error, "failed to write 0x%x bytes: ", (guint)bufsz);
		return FALSE;
	}
	if (cmdbuf->len != actual_length) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "only wrote 0x%x of 0x%x",
			    (guint)actual_length,
			    (guint)cmdbuf->len);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_ch347_device_read(FuCh347Device *self, guint8 cmd, guint8 *buf, gsize bufsz, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(self));
	gsize actual_length = 0;
	guint8 cmd_rsp;
	guint16 size_rsp;
	g_autoptr(GByteArray) cmdbuf = g_byte_array_new();

	/* pack */
	fu_byte_array_append_uint8(cmdbuf, cmd);
	fu_byte_array_append_uint16(cmdbuf, sizeof(guint32), G_LITTLE_ENDIAN);
	fu_byte_array_append_uint32(cmdbuf, bufsz, G_LITTLE_ENDIAN);
	fu_byte_array_set_size(cmdbuf, FU_CH347_PACKET_SIZE, 0x0);

	if (!g_usb_device_bulk_transfer(usb_device,
					FU_CH347_EP_IN,
					cmdbuf->data,
					cmdbuf->len,
					&actual_length,
					FU_CH347_USB_TIMEOUT,
					NULL,
					error)) {
		g_prefix_error(error, "failed to read 0x%x bytes: ", (guint)bufsz);
		return FALSE;
	}
	fu_dump_raw(G_LOG_DOMAIN, "read", cmdbuf->data, actual_length);
	if (actual_length == 0) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "returned 0 bytes");
		return FALSE;
	}

	/* debug */
	cmd_rsp = cmdbuf->data[0];
	if (cmd_rsp != cmd) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_FAILED,
			    "invalid cmd, got 0x%02x, expected 0x%02x",
			    cmd_rsp,
			    cmd);
		return FALSE;
	}
	size_rsp = fu_memread_uint16(cmdbuf->data + 0x1, G_LITTLE_ENDIAN);
	if (size_rsp != bufsz) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_FAILED,
			    "size invalid, got 0x%04x, expected 0x04%x",
			    size_rsp,
			    (guint)bufsz);
		return FALSE;
	}

	/* success */
	memcpy(buf, cmdbuf->data + 0x3, size_rsp);
	return TRUE;
}

gboolean
fu_ch347_device_send_command(FuCh347Device *self,
			     const guint8 *wbuf,
			     gsize wbufsz,
			     guint8 *rbuf,
			     gsize rbufsz,
			     FuProgress *progress,
			     GError **error)
{
	/* write */
	if (wbufsz > 0) {
		g_autoptr(GPtrArray) chunks =
		    fu_chunk_array_new(wbuf, wbufsz, 0x0, 0x0, FU_CH347_PAYLOAD_SIZE);
		for (guint i = 0; i < chunks->len; i++) {
			FuChunk *chk = g_ptr_array_index(chunks, i);
			guint8 buf[1] = {0x0};
			if (!fu_ch347_device_write(self,
						   FU_CH347_CMD_SPI_OUT,
						   fu_chunk_get_data(chk),
						   fu_chunk_get_data_sz(chk),
						   error))
				return FALSE;
			if (!fu_ch347_device_read(self,
						  FU_CH347_CMD_SPI_OUT,
						  buf,
						  sizeof(buf),
						  error))
				return FALSE;
		}
	}

	/* read */
	if (rbufsz > 0) {
		g_autoptr(GPtrArray) chunks =
		    fu_chunk_array_mutable_new(rbuf, rbufsz, 0x0, 0x0, FU_CH347_PAYLOAD_SIZE);
		g_autoptr(GByteArray) cmdbuf = g_byte_array_new();
		fu_byte_array_append_uint32(cmdbuf, rbufsz, G_LITTLE_ENDIAN);
		if (!fu_ch347_device_write(self,
					   FU_CH347_CMD_SPI_IN,
					   cmdbuf->data,
					   cmdbuf->len,
					   error))
			return FALSE;
		fu_progress_set_id(progress, G_STRLOC);
		fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_READ);
		fu_progress_set_steps(progress, chunks->len);
		for (guint i = 0; i < chunks->len; i++) {
			FuChunk *chk = g_ptr_array_index(chunks, i);
			if (!fu_ch347_device_read(self,
						  FU_CH347_CMD_SPI_IN,
						  fu_chunk_get_data_out(chk),
						  fu_chunk_get_data_sz(chk),
						  error))
				return FALSE;
			fu_progress_step_done(progress);
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_ch347_device_configure_stream(FuCh347Device *self, GError **error)
{
	guint8 data[26] = {[2] = 4,			      /* ?? */
			   [3] = 1,			      /* ?? */
			   [6] = 0,			      /* clock polarity: bit 1 */
			   [8] = 0,			      /* clock phase: bit 0 */
			   [11] = 2,			      /* ?? */
			   [12] = (self->divisor & 0x7) << 3, /* clock divisor: bits 5:3 */
			   [14] = 0,			      /* bit order: bit 7, 0=MSB */
			   [16] = 7,			      /* ?? */
			   [21] = 0}; /* CS polarity: bit 7 CS2, bit 6 CS1. 0 = active low */
	if (!fu_ch347_device_write(self, FU_CH347_CMD_SPI_SET_CFG, data, sizeof(data), error)) {
		g_prefix_error(error, "failed to configure stream: ");
		return FALSE;
	}
	if (!fu_ch347_device_read(self, FU_CH347_CMD_SPI_SET_CFG, data, 1, error)) {
		g_prefix_error(error, "failed to confirm configure stream: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

gboolean
fu_ch347_device_chip_select(FuCh347Device *self, gboolean val, GError **error)
{
	guint8 buf[10] = {
	    [0] = val ? FU_CH347_CS_ASSERT | FU_CH347_CS_CHANGE
		      : FU_CH347_CS_DEASSERT | FU_CH347_CS_CHANGE,
	    [5] = FU_CH347_CS_IGNORE /* CS2 */
	};
	return fu_ch347_device_write(self, FU_CH347_CMD_SPI_CS_CTRL, buf, sizeof(buf), error);
}

static gboolean
fu_ch347_device_setup(FuDevice *device, GError **error)
{
	FuCh347Device *self = FU_CH347_DEVICE(device);
	g_autoptr(FuCh347CfiDevice) cfi_device = NULL;

	/* FuUsbDevice->setup */
	if (!FU_DEVICE_CLASS(fu_ch347_device_parent_class)->setup(device, error))
		return FALSE;

	/* set divisor */
	if (!fu_ch347_device_configure_stream(self, error))
		return FALSE;

	/* setup SPI chip */
	cfi_device = g_object_new(FU_TYPE_CH347_CFI_DEVICE,
				  "context",
				  fu_device_get_context(device),
				  "proxy",
				  device,
				  "parent",
				  device,
				  "logical-id",
				  "SPI",
				  NULL);
	if (!fu_device_setup(FU_DEVICE(cfi_device), error))
		return FALSE;
	fu_device_add_child(device, FU_DEVICE(cfi_device));

	/* success */
	return TRUE;
}

static void
fu_ch347_device_init(FuCh347Device *self)
{
	self->divisor = 0b10;
	fu_usb_device_add_interface(FU_USB_DEVICE(self), FU_CH347_MODE1_IFACE);
	fu_device_set_name(FU_DEVICE(self), "CH347");
	fu_device_set_vendor(FU_DEVICE(self), "WinChipHead");
}

static void
fu_ch347_device_class_init(FuCh347DeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->setup = fu_ch347_device_setup;
	klass_device->to_string = fu_ch347_device_to_string;
}
