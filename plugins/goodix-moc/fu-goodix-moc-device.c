/*
 * Copyright 2016 Richard Hughes <richard@hughsie.com>
 * Copyright 2020 boger wang <boger@goodix.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-goodix-moc-device.h"
#include "fu-goodix-moc-struct.h"

struct _FuGoodixMocDevice {
	FuUsbDevice parent_instance;
	guint8 dummy_seq;
};

G_DEFINE_TYPE(FuGoodixMocDevice, fu_goodix_moc_device, FU_TYPE_USB_DEVICE)

#define GX_USB_BULK_EP_IN  (3 | 0x80)
#define GX_USB_BULK_EP_OUT (1 | 0x00)
#define GX_USB_INTERFACE   0

#define GX_USB_DATAIN_TIMEOUT	     2000 /* ms */
#define GX_USB_DATAOUT_TIMEOUT	     200  /* ms */
#define GX_FLASH_TRANSFER_BLOCK_SIZE 1000 /* 1000 */

#define FU_GOODIX_MOC_CMD1_DEFAULT 0x00

#define GX_SIZE_CRC32 4

static gboolean
fu_goodix_moc_device_cmd_send(FuGoodixMocDevice *self,
			      FuGoodixMocCmd cmd0,
			      guint8 cmd1,
			      FuGoodixMocPkgType type,
			      GByteArray *req,
			      GError **error)
{
	guint32 crc_all = 0;
	guint32 crc_hdr = 0;
	gsize actual_len = 0;
	g_autoptr(FuStructGoodixMocPkgHeader) st = fu_struct_goodix_moc_pkg_header_new();

	/* build header */
	fu_struct_goodix_moc_pkg_header_set_cmd0(st, cmd0);
	fu_struct_goodix_moc_pkg_header_set_cmd1(st, cmd1);
	fu_struct_goodix_moc_pkg_header_set_pkg_flag(st, type);
	fu_struct_goodix_moc_pkg_header_set_seq(st, self->dummy_seq++); /* reserved */
	fu_struct_goodix_moc_pkg_header_set_len(st, req->len + GX_SIZE_CRC32);
	crc_hdr = ~fu_crc8(FU_CRC_KIND_B8_STANDARD,
			   st->buf->data,
			   FU_STRUCT_GOODIX_MOC_PKG_HEADER_OFFSET_CRC8);
	fu_struct_goodix_moc_pkg_header_set_crc8(st, crc_hdr);
	fu_struct_goodix_moc_pkg_header_set_rev_crc8(st, ~crc_hdr);
	g_byte_array_append(st->buf, req->data, req->len);
	crc_all = fu_crc32(FU_CRC_KIND_B32_STANDARD, st->buf->data, st->buf->len);
	fu_byte_array_append_uint32(st->buf, crc_all, G_LITTLE_ENDIAN);

	/* send zero length package */
	if (!fu_usb_device_bulk_transfer(FU_USB_DEVICE(self),
					 GX_USB_BULK_EP_OUT,
					 NULL,
					 0,
					 NULL,
					 GX_USB_DATAOUT_TIMEOUT,
					 NULL,
					 error)) {
		g_prefix_error_literal(error, "failed to req: ");
		return FALSE;
	}
	fu_dump_full(G_LOG_DOMAIN,
		     "REQST",
		     st->buf->data,
		     st->buf->len,
		     16,
		     FU_DUMP_FLAG_SHOW_ADDRESSES);

	/* send data */
	if (!fu_usb_device_bulk_transfer(FU_USB_DEVICE(self),
					 GX_USB_BULK_EP_OUT,
					 st->buf->data,
					 st->buf->len,
					 &actual_len,
					 GX_USB_DATAOUT_TIMEOUT,
					 NULL,
					 error)) {
		g_prefix_error_literal(error, "failed to req: ");
		return FALSE;
	}
	if (actual_len != st->buf->len) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "invalid length");
		return FALSE;
	}

	/* success */
	return TRUE;
}

typedef struct {
	GByteArray *res;
	gboolean data_reply;
} FuGoodixMocDeviceHelper;

/*
 * package format
 * | zlp | ack | zlp | data |
 */
static gboolean
fu_goodix_moc_device_cmd_recv_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuGoodixMocDevice *self = FU_GOODIX_MOC_DEVICE(device);
	FuGoodixMocDeviceHelper *helper = (FuGoodixMocDeviceHelper *)user_data;
	gsize actual_len = 0;
	gsize crc_offset;
	guint16 header_len;
	guint32 crc_actual;
	guint32 crc_calculated = 0;
	guint8 header_cmd0;
	g_autoptr(GByteArray) reply = g_byte_array_new();
	g_autoptr(FuStructGoodixMocPkgHeader) st = NULL;

	fu_byte_array_set_size(reply, GX_FLASH_TRANSFER_BLOCK_SIZE, 0x00);
	if (!fu_usb_device_bulk_transfer(FU_USB_DEVICE(self),
					 GX_USB_BULK_EP_IN,
					 reply->data,
					 reply->len,
					 &actual_len, /* allowed to return short read */
					 GX_USB_DATAIN_TIMEOUT,
					 NULL,
					 error)) {
		g_prefix_error_literal(error, "failed to reply: ");
		return FALSE;
	}

	/* receive zero length package */
	fu_dump_full(G_LOG_DOMAIN,
		     "REPLY",
		     reply->data,
		     actual_len,
		     16,
		     FU_DUMP_FLAG_SHOW_ADDRESSES);
	st = fu_struct_goodix_moc_pkg_header_parse(reply->data,
						   MIN(reply->len, actual_len),
						   0x0,
						   error);
	if (st == NULL)
		return FALSE;

	/* parse package header */
	header_cmd0 = fu_struct_goodix_moc_pkg_header_get_cmd0(st);
	header_len = fu_struct_goodix_moc_pkg_header_get_len(st);
	crc_offset = FU_STRUCT_GOODIX_MOC_PKG_HEADER_SIZE + header_len - GX_SIZE_CRC32;
	crc_actual = fu_crc32(FU_CRC_KIND_B32_STANDARD, reply->data, crc_offset);
	if (!fu_memread_uint32_safe(reply->data,
				    reply->len,
				    crc_offset,
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

	/* continue after ack received */
	if (header_cmd0 == FU_GOODIX_MOC_CMD_ACK && helper->data_reply) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "continue after ack");
		return FALSE;
	}

	/* success */
	g_byte_array_append(helper->res,
			    reply->data + FU_STRUCT_GOODIX_MOC_PKG_HEADER_SIZE,
			    reply->len - FU_STRUCT_GOODIX_MOC_PKG_HEADER_SIZE);
	return TRUE;
}

static GByteArray *
fu_goodix_moc_device_cmd_recv(FuGoodixMocDevice *self, gboolean data_reply, GError **error)
{
	g_autoptr(GByteArray) res = g_byte_array_new();
	FuGoodixMocDeviceHelper helper = {
	    .data_reply = data_reply,
	    .res = res,
	};
	if (!fu_device_retry(FU_DEVICE(self), fu_goodix_moc_device_cmd_recv_cb, 5, &helper, error))
		return NULL;
	return g_steal_pointer(&res);
}

static GByteArray *
fu_goodix_moc_device_cmd_xfer(FuGoodixMocDevice *device,
			      FuGoodixMocCmd cmd0,
			      guint8 cmd1,
			      FuGoodixMocPkgType type,
			      GByteArray *req,
			      gboolean data_reply,
			      GError **error)
{
	FuGoodixMocDevice *self = FU_GOODIX_MOC_DEVICE(device);
	if (!fu_goodix_moc_device_cmd_send(self, cmd0, cmd1, type, req, error))
		return NULL;
	return fu_goodix_moc_device_cmd_recv(self, data_reply, error);
}

static gboolean
fu_goodix_moc_device_setup_version(FuGoodixMocDevice *self, GError **error)
{
	g_autofree gchar *version = NULL;
	g_autoptr(GByteArray) req = g_byte_array_new();
	g_autoptr(GByteArray) res = NULL;
	g_autoptr(FuStructGoodixMocPkgVersionRsp) st_rsp = NULL;

	fu_byte_array_append_uint8(req, 0); /* dummy */
	res = fu_goodix_moc_device_cmd_xfer(self,
					    FU_GOODIX_MOC_CMD_VERSION,
					    FU_GOODIX_MOC_CMD1_DEFAULT,
					    FU_GOODIX_MOC_PKG_TYPE_EOP,
					    req,
					    TRUE,
					    error);
	if (res == NULL)
		return FALSE;
	st_rsp = fu_struct_goodix_moc_pkg_version_rsp_parse(res->data, res->len, 0x0, error);
	if (st_rsp == NULL)
		return FALSE;
	version = fu_struct_goodix_moc_pkg_version_rsp_get_fwversion(st_rsp);
	fu_device_set_version(FU_DEVICE(self), version);
	return TRUE;
}

static gboolean
fu_goodix_moc_device_update_init(FuGoodixMocDevice *self, GError **error)
{
	g_autoptr(FuStructGoodixMocPkgRsp) st_rsp = NULL;
	g_autoptr(GByteArray) req = g_byte_array_new();
	g_autoptr(GByteArray) res = NULL;

	/* update initial */
	res = fu_goodix_moc_device_cmd_xfer(self,
					    FU_GOODIX_MOC_CMD_UPGRADE,
					    FU_GOODIX_MOC_CMD_UPGRADE_INIT,
					    FU_GOODIX_MOC_PKG_TYPE_EOP,
					    req,
					    TRUE,
					    error);
	if (res == NULL) {
		g_prefix_error_literal(error, "failed to send initial update: ");
		return FALSE;
	}

	/* check result */
	st_rsp = fu_struct_goodix_moc_pkg_rsp_parse(res->data, res->len, 0x0, error);
	if (st_rsp == NULL)
		return FALSE;
	if (fu_struct_goodix_moc_pkg_rsp_get_result(st_rsp) != FU_GOODIX_MOC_RESULT_SUCCESS) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "initial update failed [0x%x]",
			    fu_struct_goodix_moc_pkg_rsp_get_result(st_rsp));
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_goodix_moc_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuGoodixMocDevice *self = FU_GOODIX_MOC_DEVICE(device);
	g_autoptr(GByteArray) req = g_byte_array_new();
	g_autoptr(GByteArray) res = NULL;
	g_autoptr(FuStructGoodixMocPkgRsp) st_rsp = NULL;

	/* reset device */
	res = fu_goodix_moc_device_cmd_xfer(self,
					    FU_GOODIX_MOC_CMD_RESET,
					    0x03,
					    FU_GOODIX_MOC_PKG_TYPE_EOP,
					    req,
					    FALSE,
					    error);
	if (res == NULL) {
		g_prefix_error_literal(error, "failed to send reset device: ");
		return FALSE;
	}

	/* check result */
	st_rsp = fu_struct_goodix_moc_pkg_rsp_parse(res->data, res->len, 0x0, error);
	if (st_rsp == NULL)
		return FALSE;
	if (fu_struct_goodix_moc_pkg_rsp_get_result(st_rsp) != FU_GOODIX_MOC_RESULT_SUCCESS) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "failed to reset device [0x%x]",
			    fu_struct_goodix_moc_pkg_rsp_get_result(st_rsp));
		return FALSE;
	}
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gboolean
fu_goodix_moc_device_setup(FuDevice *device, GError **error)
{
	FuGoodixMocDevice *self = FU_GOODIX_MOC_DEVICE(device);

	/* FuUsbDevice->setup */
	if (!FU_DEVICE_CLASS(fu_goodix_moc_device_parent_class)->setup(device, error))
		return FALSE;

	/* ensure version */
	if (!fu_goodix_moc_device_setup_version(self, error)) {
		g_prefix_error_literal(error, "failed to get firmware version: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_goodix_moc_device_write_firmware(FuDevice *device,
				    FuFirmware *firmware,
				    FuProgress *progress,
				    FwupdInstallFlags flags,
				    GError **error)
{
	FuGoodixMocDevice *self = FU_GOODIX_MOC_DEVICE(device);
	FuGoodixMocPkgType pkg_eop = FU_GOODIX_MOC_PKG_TYPE_NORMAL;
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
	chunks = fu_chunk_array_new_from_bytes(fw,
					       FU_CHUNK_ADDR_OFFSET_NONE,
					       FU_CHUNK_PAGESZ_NONE,
					       GX_FLASH_TRANSFER_BLOCK_SIZE);

	/* don't auto-boot firmware */
	if (!fu_goodix_moc_device_update_init(self, &error_local)) {
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
		g_autoptr(GByteArray) res = NULL;
		g_autoptr(GError) error_block = NULL;
		g_autoptr(FuStructGoodixMocPkgRsp) st_rsp = NULL;

		/* prepare chunk */
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		g_byte_array_append(req, fu_chunk_get_data(chk), fu_chunk_get_data_sz(chk));

		/* the last chunk */
		if (i == fu_chunk_array_length(chunks) - 1) {
			wait_data_reply = TRUE;
			pkg_eop = FU_GOODIX_MOC_PKG_TYPE_EOP;
		}
		res = fu_goodix_moc_device_cmd_xfer(self,
						    FU_GOODIX_MOC_CMD_UPGRADE,
						    FU_GOODIX_MOC_CMD_UPGRADE_DATA,
						    pkg_eop,
						    req,
						    wait_data_reply,
						    &error_block);
		if (res == NULL) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "failed to write: %s",
				    error_block->message);
			return FALSE;
		}

		/* check update status */
		st_rsp = fu_struct_goodix_moc_pkg_rsp_parse(res->data, res->len, 0x0, error);
		if (st_rsp == NULL)
			return FALSE;
		if (wait_data_reply && fu_struct_goodix_moc_pkg_rsp_get_result(st_rsp) !=
					   FU_GOODIX_MOC_RESULT_SUCCESS) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "failed to verify firmware [0x%x]",
				    fu_struct_goodix_moc_pkg_rsp_get_result(st_rsp));
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
fu_goodix_moc_device_set_progress(FuDevice *device, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 2, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 94, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 2, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 2, "reload");
}

static void
fu_goodix_moc_device_init(FuGoodixMocDevice *self)
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
	fu_device_set_install_duration(FU_DEVICE(self), 10);
	fu_device_set_firmware_size_min(FU_DEVICE(self), 0x20000);
	fu_device_set_firmware_size_max(FU_DEVICE(self), 0x30000);
	fu_usb_device_add_interface(FU_USB_DEVICE(self), GX_USB_INTERFACE);
}

static void
fu_goodix_moc_device_class_init(FuGoodixMocDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->write_firmware = fu_goodix_moc_device_write_firmware;
	device_class->setup = fu_goodix_moc_device_setup;
	device_class->attach = fu_goodix_moc_device_attach;
	device_class->set_progress = fu_goodix_moc_device_set_progress;
}
