/*
 * Copyright 2016 Richard Hughes <richard@hughsie.com>
 * Copyright 2020 boger wang <boger@goodix.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-goodixmoc-common.h"
#include "fu-goodixmoc-device.h"

struct _FuGoodixMocDevice {
	FuUsbDevice parent_instance;
	guint8 dummy_seq;
};

G_DEFINE_TYPE(FuGoodixMocDevice, fu_goodixmoc_device, FU_TYPE_USB_DEVICE)

#define GX_USB_BULK_EP_IN  (3 | 0x80)
#define GX_USB_BULK_EP_OUT (1 | 0x00)
#define GX_USB_INTERFACE   0

#define GX_USB_DATAIN_TIMEOUT	     2000 /* ms */
#define GX_USB_DATAOUT_TIMEOUT	     200  /* ms */
#define GX_FLASH_TRANSFER_BLOCK_SIZE 1000 /* 1000 */

static gboolean
fu_goodixmoc_device_cmd_send(FuGoodixMocDevice *self,
			     guint8 cmd0,
			     guint8 cmd1,
			     GxPkgType type,
			     GByteArray *req,
			     GError **error)
{
	guint32 crc_all = 0;
	guint32 crc_hdr = 0;
	gsize actual_len = 0;
	g_autoptr(GByteArray) buf = g_byte_array_new();

	/* build header */
	fu_byte_array_append_uint8(buf, cmd0);
	fu_byte_array_append_uint8(buf, cmd1);
	fu_byte_array_append_uint8(buf, type);		    /* pkg_flag */
	fu_byte_array_append_uint8(buf, self->dummy_seq++); /* reserved */
	fu_byte_array_append_uint16(buf, req->len + GX_SIZE_CRC32, G_LITTLE_ENDIAN);
	crc_hdr = ~fu_crc8(FU_CRC_KIND_B8_STANDARD, buf->data, buf->len);
	fu_byte_array_append_uint8(buf, crc_hdr);
	fu_byte_array_append_uint8(buf, ~crc_hdr);
	g_byte_array_append(buf, req->data, req->len);
	crc_all = fu_crc32(FU_CRC_KIND_B32_STANDARD, buf->data, buf->len);
	fu_byte_array_append_uint32(buf, crc_all, G_LITTLE_ENDIAN);

	/* send zero length package */
	if (!fu_usb_device_bulk_transfer(FU_USB_DEVICE(self),
					 GX_USB_BULK_EP_OUT,
					 NULL,
					 0,
					 NULL,
					 GX_USB_DATAOUT_TIMEOUT,
					 NULL,
					 error)) {
		g_prefix_error(error, "failed to req: ");
		return FALSE;
	}
	fu_dump_full(G_LOG_DOMAIN, "REQST", buf->data, buf->len, 16, FU_DUMP_FLAGS_SHOW_ADDRESSES);

	/* send data */
	if (!fu_usb_device_bulk_transfer(FU_USB_DEVICE(self),
					 GX_USB_BULK_EP_OUT,
					 buf->data,
					 buf->len,
					 &actual_len,
					 GX_USB_DATAOUT_TIMEOUT,
					 NULL,
					 error)) {
		g_prefix_error(error, "failed to req: ");
		return FALSE;
	}
	if (actual_len != buf->len) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "invalid length");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_goodixmoc_device_cmd_recv(FuGoodixMocDevice *self,
			     GxfpCmdResp *presponse,
			     gboolean data_reply,
			     GError **error)
{
	guint32 crc_actual = 0;
	guint32 crc_calculated = 0;
	gsize actual_len = 0;
	gsize offset = 0;

	g_return_val_if_fail(presponse != NULL, FALSE);

	/*
	 * package format
	 * | zlp | ack | zlp | data |
	 */
	while (1) {
		guint16 header_len = 0x0;
		guint8 header_cmd0 = 0x0;
		g_autoptr(GByteArray) reply = g_byte_array_new();
		fu_byte_array_set_size(reply, GX_FLASH_TRANSFER_BLOCK_SIZE, 0x00);
		if (!fu_usb_device_bulk_transfer(FU_USB_DEVICE(self),
						 GX_USB_BULK_EP_IN,
						 reply->data,
						 reply->len,
						 &actual_len, /* allowed to return short read */
						 GX_USB_DATAIN_TIMEOUT,
						 NULL,
						 error)) {
			g_prefix_error(error, "failed to reply: ");
			return FALSE;
		}

		/* receive zero length package */
		if (actual_len == 0)
			continue;
		fu_dump_full(G_LOG_DOMAIN,
			     "REPLY",
			     reply->data,
			     actual_len,
			     16,
			     FU_DUMP_FLAGS_SHOW_ADDRESSES);

		/* parse package header */
		if (!fu_memread_uint8_safe(reply->data, reply->len, 0x0, &header_cmd0, error))
			return FALSE;
		if (!fu_memread_uint16_safe(reply->data,
					    reply->len,
					    0x4,
					    &header_len,
					    G_LITTLE_ENDIAN,
					    error))
			return FALSE;
		offset = sizeof(GxfpPkgHeader) + header_len - GX_SIZE_CRC32;
		crc_actual = fu_crc32(FU_CRC_KIND_B32_STANDARD, reply->data, offset);
		if (!fu_memread_uint32_safe(reply->data,
					    reply->len,
					    offset,
					    &crc_calculated,
					    G_LITTLE_ENDIAN,
					    error))
			return FALSE;
		if (crc_actual != crc_calculated) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "invalid checksum, got 0x%x, expected 0x%x",
				    crc_calculated,
				    crc_actual);
			return FALSE;
		}

		/* parse package data */
		if (!fu_memread_uint8_safe(reply->data,
					   reply->len,
					   sizeof(GxfpPkgHeader) + 0x00,
					   &presponse->result,
					   error))
			return FALSE;
		if (header_cmd0 == GX_CMD_ACK) {
			if (header_len == 0) {
				g_set_error_literal(error,
						    FWUPD_ERROR,
						    FWUPD_ERROR_INTERNAL,
						    "invalid bufsz");
				return FALSE;
			}
			if (!fu_memread_uint8_safe(reply->data,
						   reply->len,
						   sizeof(GxfpPkgHeader) + 0x01,
						   &presponse->ack_msg.cmd,
						   error))
				return FALSE;
		} else if (header_cmd0 == GX_CMD_VERSION) {
			if (!fu_memcpy_safe((guint8 *)&presponse->version_info,
					    sizeof(presponse->version_info),
					    0x0, /* dst */
					    reply->data,
					    reply->len,
					    sizeof(GxfpPkgHeader) + 0x01, /* src */
					    sizeof(GxfpVersionInfo),
					    error))
				return FALSE;
		}

		/* continue after ack received */
		if (header_cmd0 == GX_CMD_ACK && data_reply)
			continue;
		break;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_goodixmoc_device_cmd_xfer(FuGoodixMocDevice *device,
			     guint8 cmd0,
			     guint8 cmd1,
			     GxPkgType type,
			     GByteArray *req,
			     GxfpCmdResp *presponse,
			     gboolean data_reply,
			     GError **error)
{
	FuGoodixMocDevice *self = FU_GOODIXMOC_DEVICE(device);
	if (!fu_goodixmoc_device_cmd_send(self, cmd0, cmd1, type, req, error))
		return FALSE;
	return fu_goodixmoc_device_cmd_recv(self, presponse, data_reply, error);
}

static gboolean
fu_goodixmoc_device_setup_version(FuGoodixMocDevice *self, GError **error)
{
	GxfpCmdResp rsp = {0};
	g_autofree gchar *version = NULL;
	g_autoptr(GByteArray) req = g_byte_array_new();

	fu_byte_array_append_uint8(req, 0); /* dummy */
	if (!fu_goodixmoc_device_cmd_xfer(self,
					  GX_CMD_VERSION,
					  GX_CMD1_DEFAULT,
					  GX_PKG_TYPE_EOP,
					  req,
					  &rsp,
					  TRUE,
					  error))
		return FALSE;
	version = g_strndup((const gchar *)rsp.version_info.fwversion,
			    sizeof(rsp.version_info.fwversion));
	fu_device_set_version(FU_DEVICE(self), version);
	return TRUE;
}

static gboolean
fu_goodixmoc_device_update_init(FuGoodixMocDevice *self, GError **error)
{
	GxfpCmdResp rsp = {0};
	g_autoptr(GByteArray) req = g_byte_array_new();

	/* update initial */
	if (!fu_goodixmoc_device_cmd_xfer(self,
					  GX_CMD_UPGRADE,
					  GX_CMD_UPGRADE_INIT,
					  GX_PKG_TYPE_EOP,
					  req,
					  &rsp,
					  TRUE,
					  error)) {
		g_prefix_error(error, "failed to send initial update: ");
		return FALSE;
	}

	/* check result */
	if (rsp.result != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "initial update failed [0x%x]",
			    rsp.result);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_goodixmoc_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuGoodixMocDevice *self = FU_GOODIXMOC_DEVICE(device);
	GxfpCmdResp rsp = {0};
	g_autoptr(GByteArray) req = g_byte_array_new();

	/* reset device */
	if (!fu_goodixmoc_device_cmd_xfer(self,
					  GX_CMD_RESET,
					  0x03,
					  GX_PKG_TYPE_EOP,
					  req,
					  &rsp,
					  FALSE,
					  error)) {
		g_prefix_error(error, "failed to send reset device: ");
		return FALSE;
	}

	/* check result */
	if (rsp.result != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "failed to reset device [0x%x]",
			    rsp.result);
		return FALSE;
	}
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gboolean
fu_goodixmoc_device_setup(FuDevice *device, GError **error)
{
	FuGoodixMocDevice *self = FU_GOODIXMOC_DEVICE(device);

	/* FuUsbDevice->setup */
	if (!FU_DEVICE_CLASS(fu_goodixmoc_device_parent_class)->setup(device, error))
		return FALSE;

	/* ensure version */
	if (!fu_goodixmoc_device_setup_version(self, error)) {
		g_prefix_error(error, "failed to get firmware version: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_goodixmoc_device_write_firmware(FuDevice *device,
				   FuFirmware *firmware,
				   FuProgress *progress,
				   FwupdInstallFlags flags,
				   GError **error)
{
	FuGoodixMocDevice *self = FU_GOODIXMOC_DEVICE(device);
	GxPkgType pkg_eop = GX_PKG_TYPE_NORMAL;
	GxfpCmdResp rsp = {0};
	gboolean wait_data_reply = FALSE;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(FuChunkArray) chunks = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, "init");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 99, NULL);

	/* get default image */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;

	/* build packets */
	chunks = fu_chunk_array_new_from_bytes(fw, 0x00, GX_FLASH_TRANSFER_BLOCK_SIZE);

	/* don't auto-boot firmware */
	if (!fu_goodixmoc_device_update_init(self, &error_local)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "failed to initial update: %s",
			    error_local->message);
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* write each block */
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;
		g_autoptr(GByteArray) req = g_byte_array_new();
		g_autoptr(GError) error_block = NULL;

		/* prepare chunk */
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		g_byte_array_append(req, fu_chunk_get_data(chk), fu_chunk_get_data_sz(chk));

		/* the last chunk */
		if (i == fu_chunk_array_length(chunks) - 1) {
			wait_data_reply = TRUE;
			pkg_eop = GX_PKG_TYPE_EOP;
		}
		if (!fu_goodixmoc_device_cmd_xfer(self,
						  GX_CMD_UPGRADE,
						  GX_CMD_UPGRADE_DATA,
						  pkg_eop,
						  req,
						  &rsp,
						  wait_data_reply,
						  &error_block)) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "failed to write: %s",
				    error_block->message);
			return FALSE;
		}

		/* check update status */
		if (wait_data_reply && rsp.result != 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "failed to verify firmware [0x%x]",
				    rsp.result);
			return FALSE;
		}

		/* update progress */
		fu_progress_set_percentage_full(fu_progress_get_child(progress),
						(gsize)i + 1,
						(gsize)fu_chunk_array_length(chunks));
	}
	fu_progress_step_done(progress);

	/* success! */
	return TRUE;
}

static void
fu_goodixmoc_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 2, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 94, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 2, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 2, "reload");
}

static void
fu_goodixmoc_device_init(FuGoodixMocDevice *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SELF_RECOVERY);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_USE_RUNTIME_VERSION);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PLAIN);
	fu_device_set_remove_delay(FU_DEVICE(self), 5000);
	fu_device_add_protocol(FU_DEVICE(self), "com.goodix.goodixmoc");
	fu_device_set_name(FU_DEVICE(self), "Fingerprint Sensor");
	fu_device_set_summary(FU_DEVICE(self), "Match-On-Chip fingerprint sensor");
	fu_device_set_vendor(FU_DEVICE(self), "Goodix");
	fu_device_set_install_duration(FU_DEVICE(self), 10);
	fu_device_set_firmware_size_min(FU_DEVICE(self), 0x20000);
	fu_device_set_firmware_size_max(FU_DEVICE(self), 0x30000);
	fu_usb_device_add_interface(FU_USB_DEVICE(self), GX_USB_INTERFACE);
}

static void
fu_goodixmoc_device_class_init(FuGoodixMocDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->write_firmware = fu_goodixmoc_device_write_firmware;
	device_class->setup = fu_goodixmoc_device_setup;
	device_class->attach = fu_goodixmoc_device_attach;
	device_class->set_progress = fu_goodixmoc_device_set_progress;
}
