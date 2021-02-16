/*
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2020 boger wang <boger@goodix.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-chunk.h"

#include "fu-goodixmoc-common.h"
#include "fu-goodixmoc-device.h"

struct _FuGoodixMocDevice {
	FuUsbDevice	parent_instance;
	guint8		dummy_seq;
};

G_DEFINE_TYPE (FuGoodixMocDevice, fu_goodixmoc_device, FU_TYPE_USB_DEVICE)

#define GX_USB_BULK_EP_IN		(3 | 0x80)
#define GX_USB_BULK_EP_OUT		(1 | 0x00)
#define GX_USB_INTERFACE		0

#define GX_USB_DATAIN_TIMEOUT		2000	/* ms */
#define GX_USB_DATAOUT_TIMEOUT		200	/* ms */
#define GX_FLASH_TRANSFER_BLOCK_SIZE	1000	/* 1000 */

static gboolean
goodixmoc_device_cmd_send (FuGoodixMocDevice *self,
			   guint8      cmd0,
			   guint8      cmd1,
			   GxPkgType   type,
			   GByteArray *req,
			   GError    **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));
	guint32 crc_all = 0;
	guint32 crc_hdr = 0;
	gsize actual_len = 0;
	g_autoptr(GByteArray) buf = g_byte_array_new ();

	/* build header */
	fu_byte_array_append_uint8 (buf, cmd0);
	fu_byte_array_append_uint8 (buf, cmd1);
	fu_byte_array_append_uint8 (buf, type);			/* pkg_flag */
	fu_byte_array_append_uint8 (buf, self->dummy_seq++);	/* reserved */
	fu_byte_array_append_uint16 (buf, req->len + GX_SIZE_CRC32, G_LITTLE_ENDIAN);
	crc_hdr = fu_common_crc8 (buf->data, buf->len);
	fu_byte_array_append_uint8 (buf, crc_hdr);
	fu_byte_array_append_uint8 (buf, ~crc_hdr);
	g_byte_array_append (buf, req->data, req->len);
	crc_all = fu_common_crc32 (buf->data, buf->len);
	fu_byte_array_append_uint32 (buf, crc_all, G_LITTLE_ENDIAN);

	/* send zero length package */
	if (!g_usb_device_bulk_transfer (usb_device,
					 GX_USB_BULK_EP_OUT,
					 NULL,
					 0,
					 NULL,
					 GX_USB_DATAOUT_TIMEOUT, NULL, error)) {
		g_prefix_error (error, "failed to req: ");
		return FALSE;
	}
	if (g_getenv ("FWUPD_GOODIXFP_VERBOSE") != NULL) {
		fu_common_dump_full (G_LOG_DOMAIN, "REQST",
				     buf->data, buf->len, 16,
				     FU_DUMP_FLAGS_SHOW_ADDRESSES);
	}

	/* send data */
	if (!g_usb_device_bulk_transfer (usb_device,
					 GX_USB_BULK_EP_OUT,
					 buf->data,
					 buf->len,
					 &actual_len,
					 GX_USB_DATAOUT_TIMEOUT, NULL, error)) {
		g_prefix_error (error, "failed to req: ");
		return FALSE;
	}
	if (actual_len != buf->len) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "invalid length");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
goodixmoc_device_cmd_recv (FuGoodixMocDevice *self,
			   GxfpCmdResp *presponse,
			   gboolean     data_reply,
			   GError     **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));
	guint32 crc_actual = 0;
	guint32 crc_calculated = 0;
	gsize actual_len = 0;
	gsize offset = 0;

	g_return_val_if_fail (presponse != NULL, FALSE);

	/*
	* package format
	* | zlp | ack | zlp | data |
	*/
	while (1) {
		guint16 header_len = 0x0;
		guint8 header_cmd0 = 0x0;
		g_autoptr(GByteArray) reply = g_byte_array_new ();
		fu_byte_array_set_size (reply, GX_FLASH_TRANSFER_BLOCK_SIZE);
		if (!g_usb_device_bulk_transfer (usb_device,
						 GX_USB_BULK_EP_IN,
						 reply->data,
						 reply->len,
						 &actual_len, /* allowed to return short read */
						 GX_USB_DATAIN_TIMEOUT,
						 NULL, error)) {
			g_prefix_error (error, "failed to reply: ");
			return FALSE;
		}

		/* receive zero length package */
		if (actual_len == 0)
			continue;
		if (g_getenv ("FWUPD_GOODIXFP_VERBOSE") != NULL) {
			fu_common_dump_full (G_LOG_DOMAIN, "REPLY",
					     reply->data, actual_len, 16,
					     FU_DUMP_FLAGS_SHOW_ADDRESSES);
		}

		/* parse package header */
		if (!fu_common_read_uint8_safe (reply->data, reply->len, 0x0,
						&header_cmd0, error))
			return FALSE;
		if (!fu_common_read_uint16_safe	(reply->data, reply->len, 0x4,
						 &header_len, G_LITTLE_ENDIAN,
						 error))
			return FALSE;
		offset = sizeof(GxfpPkgHeader) + header_len - GX_SIZE_CRC32;
		crc_actual = fu_common_crc32 (reply->data, offset);
		if (!fu_common_read_uint32_safe	(reply->data,
						 reply->len,
						 offset,
						 &crc_calculated,
						 G_LITTLE_ENDIAN,
						 error))
			return FALSE;
		if (crc_actual != crc_calculated) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "invalid checksum, got 0x%x, expected 0x%x",
				     crc_calculated, crc_actual);
			return FALSE;
		}

		/* parse package data */
		if (!fu_common_read_uint8_safe (reply->data, reply->len,
						sizeof(GxfpPkgHeader) + 0x00,
						&presponse->result, error))
			return FALSE;
		if (header_cmd0 == GX_CMD_ACK) {
			if (header_len == 0) {
				g_set_error_literal (error,
						     FWUPD_ERROR,
						     FWUPD_ERROR_INTERNAL,
						     "invalid bufsz");
				return FALSE;
			}
			if (!fu_common_read_uint8_safe (reply->data, reply->len,
							sizeof(GxfpPkgHeader) + 0x01,
							&presponse->ack_msg.cmd, error))
				return FALSE;
		} else if (header_cmd0 == GX_CMD_VERSION) {
			if (!fu_memcpy_safe ((guint8 *) &presponse->version_info,
					     sizeof(presponse->version_info), 0x0,	/* dst */
					     reply->data, reply->len,
					     sizeof(GxfpPkgHeader) + 0x01,		/* src */
					     sizeof(GxfpVersionInfo), error))
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
fu_goodixmoc_device_cmd_xfer (FuGoodixMocDevice	*device,
			      guint8		 cmd0,
			      guint8		 cmd1,
			      GxPkgType		 type,
			      GByteArray	*req,
			      GxfpCmdResp	*presponse,
			      gboolean		 data_reply,
			      GError		**error)
{
	FuGoodixMocDevice *self = FU_GOODIXMOC_DEVICE(device);
	if (!goodixmoc_device_cmd_send (self, cmd0, cmd1, type, req, error))
		return FALSE;
	return goodixmoc_device_cmd_recv (self, presponse, data_reply, error);
}

static gboolean
fu_goodixmoc_device_setup_version (FuGoodixMocDevice *self, GError **error)
{
	GxfpCmdResp rsp = { 0 };
	g_autofree gchar *version = NULL;
	g_autoptr(GByteArray) req = g_byte_array_new ();

	fu_byte_array_append_uint8 (req, 0);	/* dummy */
	if (!fu_goodixmoc_device_cmd_xfer (self, GX_CMD_VERSION, GX_CMD1_DEFAULT,
					  GX_PKG_TYPE_EOP, req, &rsp, TRUE, error))
		return FALSE;
	version = g_strndup ((const gchar *) rsp.version_info.fwversion,
			     sizeof(rsp.version_info.fwversion));
	fu_device_set_version (FU_DEVICE (self), version);
	return TRUE;
}

static gboolean
fu_goodixmoc_device_update_init (FuGoodixMocDevice *self, GError **error)
{
	GxfpCmdResp rsp = { 0 };
	g_autoptr(GByteArray) req = g_byte_array_new ();

	/* update initial */
	if (!fu_goodixmoc_device_cmd_xfer (self, GX_CMD_UPGRADE, GX_CMD_UPGRADE_INIT,
					   GX_PKG_TYPE_EOP,
					   req,
					   &rsp,
					   TRUE,
					   error)) {
		g_prefix_error (error, "failed to send initial update: ");
		return FALSE;
	}

	/* check result */
	if (rsp.result != 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_WRITE,
			     "initial update failed [0x%x]",
			     rsp.result);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_goodixmoc_device_attach (FuDevice *device, GError **error)
{
	FuGoodixMocDevice *self = FU_GOODIXMOC_DEVICE(device);
	GxfpCmdResp rsp = { 0 };
	g_autoptr(GByteArray) req = g_byte_array_new ();

	/* reset device */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_RESTART);
	if (!fu_goodixmoc_device_cmd_xfer (self, GX_CMD_RESET, 0x03,
					   GX_PKG_TYPE_EOP,
					   req,
					   &rsp,
					   FALSE,
					   error)) {
		g_prefix_error (error, "failed to send reset device: ");
		return FALSE;
	}

	/* check result */
	if (rsp.result != 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_WRITE,
			     "failed to reset device [0x%x]",
			     rsp.result);
		return FALSE;
	}
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gboolean
fu_goodixmoc_device_open (FuUsbDevice *device, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (device);
	return g_usb_device_claim_interface (usb_device, GX_USB_INTERFACE,
					     G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
					     error);
}

static gboolean
fu_goodixmoc_device_setup (FuDevice *device, GError **error)
{
	FuGoodixMocDevice *self = FU_GOODIXMOC_DEVICE (device);

	/* ensure version */
	if (!fu_goodixmoc_device_setup_version (self, error)) {
		g_prefix_error (error, "failed to get firmware version: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_goodixmoc_device_write_firmware (FuDevice *device,
				    FuFirmware *firmware,
				    FwupdInstallFlags flags,
				    GError  **error)
{
	FuGoodixMocDevice *self = FU_GOODIXMOC_DEVICE(device);
	GxPkgType pkg_eop = GX_PKG_TYPE_NORMAL;
	GxfpCmdResp rsp = { 0 };
	gboolean wait_data_reply = FALSE;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GPtrArray) chunks = NULL;

	/* get default image */
	fw = fu_firmware_get_image_default_bytes (firmware, error);
	if (fw == NULL)
		return FALSE;

	/* build packets */
	chunks = fu_chunk_array_new_from_bytes (fw,
						0x00,
						0x00, /* page_sz */
						GX_FLASH_TRANSFER_BLOCK_SIZE);

	/* don't auto-boot firmware */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_WRITE);
	if (!fu_goodixmoc_device_update_init (self, &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_WRITE,
			     "failed to initial update: %s",
			     error_local->message);
		return FALSE;
	}

	/* write each block */
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index (chunks, i);
		g_autoptr(GByteArray) req = g_byte_array_new ();
		g_autoptr(GError) error_block = NULL;

		g_byte_array_append (req,
				     fu_chunk_get_data (chk),
				     fu_chunk_get_data_sz (chk));

		/* the last chunk */
		if (i == chunks->len - 1) {
			wait_data_reply = TRUE;
			pkg_eop = GX_PKG_TYPE_EOP;
		}
		if (!fu_goodixmoc_device_cmd_xfer (self,
						   GX_CMD_UPGRADE,
						   GX_CMD_UPGRADE_DATA,
						   pkg_eop,
						   req,
						   &rsp,
						   wait_data_reply,
						   &error_block)) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_WRITE,
				     "failed to write: %s",
				     error_block->message);
			return FALSE;
		}

		/* check update status */
		if (wait_data_reply && rsp.result != 0) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_WRITE,
				     "failed to verify firmware [0x%x]",
				     rsp.result);
			return FALSE;
		}

		/* update progress */
		fu_device_set_progress_full (device, (gsize) i, (gsize) chunks->len);
	}

	/* success! */
	return TRUE;
}

static void
fu_goodixmoc_device_init (FuGoodixMocDevice *self)
{
	fu_device_add_flag (FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag (FU_DEVICE(self), FWUPD_DEVICE_FLAG_SELF_RECOVERY);
	fu_device_add_flag (FU_DEVICE(self), FWUPD_DEVICE_FLAG_USE_RUNTIME_VERSION);
	fu_device_set_version_format (FU_DEVICE(self), FWUPD_VERSION_FORMAT_PLAIN);
	fu_device_set_remove_delay (FU_DEVICE(self), 5000);
	fu_device_set_protocol (FU_DEVICE (self), "com.goodix.goodixmoc");
	fu_device_set_name (FU_DEVICE(self), "Fingerprint Sensor");
	fu_device_set_summary (FU_DEVICE(self), "Match-On-Chip Fingerprint Sensor");
	fu_device_set_vendor (FU_DEVICE(self), "Goodix");
	fu_device_set_install_duration (FU_DEVICE(self), 10);
	fu_device_set_firmware_size_min (FU_DEVICE(self), 0x20000);
	fu_device_set_firmware_size_max (FU_DEVICE(self), 0x30000);
}

static void
fu_goodixmoc_device_class_init(FuGoodixMocDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	FuUsbDeviceClass *klass_usb_device = FU_USB_DEVICE_CLASS(klass);
	klass_device->write_firmware = fu_goodixmoc_device_write_firmware;
	klass_device->setup = fu_goodixmoc_device_setup;
	klass_device->attach = fu_goodixmoc_device_attach;
	klass_usb_device->open = fu_goodixmoc_device_open;
}
