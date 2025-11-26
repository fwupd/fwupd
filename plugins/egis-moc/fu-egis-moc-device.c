/*
 * Copyright 2025 Jason Huang <jason.huang@egistec.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-egis-moc-common.h"
#include "fu-egis-moc-device.h"
#include "fu-egis-moc-struct.h"

struct _FuEgisMocDevice {
	FuUsbDevice parent_instance;
};

G_DEFINE_TYPE(FuEgisMocDevice, fu_egis_moc_device, FU_TYPE_USB_DEVICE)

#define FU_EGIS_MOC_USB_BULK_EP_IN  (1 | 0x80)
#define FU_EGIS_MOC_USB_BULK_EP_OUT (2 | 0x00)
#define FU_EGIS_MOC_USB_INTERFACE   0

#define FU_EGIS_MOC_USB_TRANSFER_TIMEOUT      1500 /* ms */
#define FU_EGIS_MOC_FLASH_TRANSFER_BLOCK_SIZE 4096 /* byte */

#define FU_EGIS_MOC_OTA_CHALLENGE_SIZE	   32
#define FU_EGIS_MOC_HMAC_SHA256_SIZE	   32
#define FU_EGIS_MOC_OTA_CHALLENGE_HMAC_KEY "EgistecUsbVcTest"

#define FU_EGIS_MOC_APDU_VERSION_LEN 0x0C

static guint16
fu_egis_moc_device_pkg_header_checksum(GByteArray *buf)
{
	guint32 csum = 0;
	guint16 csum_be = 0;
	csum = fu_egis_moc_checksum_add(csum, buf->data, 8);
	if (buf->len > 10)
		csum = fu_egis_moc_checksum_add(csum, buf->data + 10, buf->len - 10);
	csum_be = fu_egis_moc_checksum_finish(csum);
	csum_be = csum_be >> 8 | csum_be << 8;
	return csum_be;
}

static gboolean
fu_egis_moc_device_ctrl_cmd(FuEgisMocDevice *self,
			    FuEgisMocCmd cmd,
			    guint16 value,
			    guint16 index,
			    guint8 *data,
			    gsize length,
			    gboolean device2host,
			    GError **error)
{
	gsize actual_len = 0;

	if (!fu_usb_device_control_transfer(FU_USB_DEVICE(self),
					    device2host ? FU_USB_DIRECTION_DEVICE_TO_HOST
							: FU_USB_DIRECTION_HOST_TO_DEVICE,
					    FU_USB_REQUEST_TYPE_VENDOR,
					    FU_USB_RECIPIENT_DEVICE,
					    cmd,
					    value,
					    index,
					    data,
					    length,
					    length ? &actual_len : NULL,
					    FU_EGIS_MOC_USB_TRANSFER_TIMEOUT,
					    NULL,
					    error)) {
		fwupd_error_convert(error);
		return FALSE;
	}
	if (actual_len != length) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "only sent 0x%04x of 0x%04x",
			    (guint)actual_len,
			    (guint)length);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_egis_moc_device_cmd_send(FuEgisMocDevice *self, GByteArray *req, GError **error)
{
	gsize actual_len = 0;
	g_autoptr(FuStructEgisMocPkgHeader) st_hdr = fu_struct_egis_moc_pkg_header_new();

	/* build header */
	fu_struct_egis_moc_pkg_header_set_sync(st_hdr, 0x45474953);
	fu_struct_egis_moc_pkg_header_set_id(st_hdr, 0x00000001);
	fu_struct_egis_moc_pkg_header_set_len(st_hdr, req->len);
	g_byte_array_append(st_hdr, req->data, req->len);
	fu_struct_egis_moc_pkg_header_set_chksum(st_hdr,
						 fu_egis_moc_device_pkg_header_checksum(st_hdr));

	/* send data */
	if (!fu_usb_device_bulk_transfer(FU_USB_DEVICE(self),
					 FU_EGIS_MOC_USB_BULK_EP_OUT,
					 st_hdr->data,
					 st_hdr->len,
					 &actual_len,
					 FU_EGIS_MOC_USB_TRANSFER_TIMEOUT,
					 NULL,
					 error)) {
		g_prefix_error_literal(error, "failed to req: ");
		return FALSE;
	}
	if (actual_len != st_hdr->len) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "invalid length");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_egis_moc_device_cmd_recv_cb(FuDevice *self, gpointer user_data, GError **error)
{
	gsize actual_len = 0;
	guint32 csum = 0;
	guint16 status = 0;
	GByteArray *buf_payload = (GByteArray *)user_data;
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(FuStructEgisMocPkgHeader) st_hdr = NULL;

	/* package format = | zlp | ack | zlp | data | */
	fu_byte_array_set_size(buf, FU_EGIS_MOC_FLASH_TRANSFER_BLOCK_SIZE, 0x00);
	if (!fu_usb_device_bulk_transfer(FU_USB_DEVICE(self),
					 FU_EGIS_MOC_USB_BULK_EP_IN,
					 buf->data,
					 buf->len,
					 &actual_len, /* allowed to return short read */
					 FU_EGIS_MOC_USB_TRANSFER_TIMEOUT,
					 NULL,
					 error)) {
		g_prefix_error_literal(error, "failed to reply: ");
		return FALSE;
	}
	g_byte_array_set_size(buf, actual_len);
	if (buf->len < 2) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "invalid data");
		return FALSE;
	}
	fu_dump_full(G_LOG_DOMAIN, "reply", buf->data, buf->len, 16, FU_DUMP_FLAGS_SHOW_ADDRESSES);

	/* parse package header */
	st_hdr = fu_struct_egis_moc_pkg_header_parse(buf->data, buf->len, 0x0, error);
	if (st_hdr == NULL)
		return FALSE;
	csum = fu_egis_moc_device_pkg_header_checksum(buf);
	if (fu_struct_egis_moc_pkg_header_get_chksum(st_hdr) != csum) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "invalid checksum, got 0x%x, expected 0x%x",
			    csum,
			    fu_struct_egis_moc_pkg_header_get_chksum(st_hdr));
		return FALSE;
	}
	if (!fu_memread_uint16_safe(buf->data,
				    buf->len,
				    buf->len - 2,
				    &status,
				    G_BIG_ENDIAN,
				    error))
		return FALSE;
	if (status != FU_EGIS_MOC_STATUS_SUCCESS) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "status error, 0x%x",
			    status);
		return FALSE;
	}

	/* copy out payload */
	if (!fu_memcpy_safe(buf_payload->data,
			    buf_payload->len,
			    0,
			    buf->data,
			    buf->len,
			    FU_STRUCT_EGIS_MOC_PKG_HEADER_SIZE,
			    buf->len - FU_STRUCT_EGIS_MOC_PKG_HEADER_SIZE - sizeof(status),
			    error))
		return FALSE;

	/* success */
	return TRUE;
}

static GByteArray *
fu_egis_moc_device_fw_cmd(FuEgisMocDevice *device,
			  FuStructEgisMocCmdReq *st_req,
			  gsize bufsz,
			  GError **error)
{
	FuEgisMocDevice *self = FU_EGIS_MOC_DEVICE(device);
	g_autoptr(GByteArray) buf = g_byte_array_new();

	if (!fu_egis_moc_device_cmd_send(self, st_req, error))
		return NULL;
	fu_byte_array_set_size(buf, bufsz, 0x00);
	if (!fu_device_retry(FU_DEVICE(self), fu_egis_moc_device_cmd_recv_cb, 10, buf, error))
		return NULL;

	/* success */
	return g_steal_pointer(&buf);
}

static gboolean
fu_egis_moc_device_ensure_version(FuEgisMocDevice *self, GError **error)
{
	g_autofree gchar *version = NULL;
	g_autoptr(FuStructEgisMocCmdReq) st_req = fu_struct_egis_moc_cmd_req_new();
	g_autoptr(GByteArray) buf = NULL;

	fu_struct_egis_moc_cmd_req_set_ins(st_req, FU_EGIS_MOC_CMD_APDU_VERSION);
	fu_struct_egis_moc_cmd_req_set_lc3(st_req, FU_EGIS_MOC_APDU_VERSION_LEN);

	buf = fu_egis_moc_device_fw_cmd(self, st_req, FU_STRUCT_EGIS_MOC_VERSION_INFO_SIZE, error);
	if (buf == NULL)
		return FALSE;
	if (buf->len < 3) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "invalid version data");
		return FALSE;
	}
	version = fu_strsafe((const gchar *)buf->data + 3, buf->len - 3);
	fu_device_set_version(FU_DEVICE(self), version);

	/* success */
	return TRUE;
}

static gboolean
fu_egis_moc_device_update_init(FuEgisMocDevice *self, GError **error)
{
	guint8 challenge[FU_EGIS_MOC_OTA_CHALLENGE_SIZE] = {0};
	guchar hmac_key[FU_EGIS_MOC_HMAC_SHA256_SIZE] = FU_EGIS_MOC_OTA_CHALLENGE_HMAC_KEY;
	guint8 digest[FU_EGIS_MOC_HMAC_SHA256_SIZE] = {0};
	gsize digest_len = FU_EGIS_MOC_HMAC_SHA256_SIZE;
	g_autoptr(GHmac) hmac = g_hmac_new(G_CHECKSUM_SHA256, (guchar *)hmac_key, sizeof(hmac_key));

	/* get challenge */
	if (!fu_egis_moc_device_ctrl_cmd(self,
					 FU_EGIS_MOC_CMD_CHALLENGE_GET,
					 0x0,
					 0x0,
					 challenge,
					 sizeof(challenge),
					 TRUE,
					 error)) {
		g_prefix_error_literal(error, "failed to get challenge: ");
		return FALSE;
	}
	g_hmac_update(hmac, (guchar *)challenge, sizeof(challenge));
	g_hmac_get_digest(hmac, digest, &digest_len);

	/* switch into OTA Mode */
	if (!fu_egis_moc_device_ctrl_cmd(self,
					 FU_EGIS_MOC_CMD_ENTER_OTA_MODE,
					 0x0,
					 0x0,
					 digest,
					 sizeof(digest),
					 FALSE,
					 error)) {
		g_prefix_error_literal(error, "failed to go to OTA mode: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_egis_moc_device_ensure_op_mode(FuEgisMocDevice *self, GError **error)
{
	guint8 op_mode[8] = {0};

	if (!fu_egis_moc_device_ctrl_cmd(self,
					 FU_EGIS_MOC_CMD_OP_MODE_GET,
					 0x0,
					 0x0,
					 op_mode,
					 sizeof(op_mode),
					 TRUE,
					 error)) {
		g_prefix_error_literal(error, "failed to get mode: ");
		return FALSE;
	}
	if (op_mode[0] == FU_EGIS_MOC_OP_MODE_BOOTLOADER) {
		fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	} else {
		fu_device_remove_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_egis_moc_device_setup(FuDevice *device, GError **error)
{
	FuEgisMocDevice *self = FU_EGIS_MOC_DEVICE(device);

	/* FuUsbDevice->setup */
	if (!FU_DEVICE_CLASS(fu_egis_moc_device_parent_class)->setup(device, error))
		return FALSE;

	if (!fu_egis_moc_device_ensure_op_mode(self, error)) {
		g_prefix_error_literal(error, "failed to get device mode: ");
		return FALSE;
	}
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		fu_device_set_version(FU_DEVICE(self), "0.0.0.1");
	} else {
		if (!fu_egis_moc_device_ensure_version(self, error)) {
			g_prefix_error_literal(error, "failed to get firmware version: ");
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_egis_moc_device_write_packets(FuEgisMocDevice *self,
				 FuChunkArray *chunks,
				 FuProgress *progress,
				 GError **error)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;
		g_autoptr(GByteArray) req = g_byte_array_new();
		guint32 offset;

		/* prepare chunk */
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		if (i == fu_chunk_array_length(chunks) - 1) {
			g_byte_array_append(req,
					    fu_chunk_get_data(chk),
					    fu_chunk_get_data_sz(chk) -
						FU_EGIS_MOC_HMAC_SHA256_SIZE);
		} else {
			g_byte_array_append(req, fu_chunk_get_data(chk), fu_chunk_get_data_sz(chk));
		}
		offset = fu_chunk_get_address(chk);
		if (!fu_egis_moc_device_ctrl_cmd(self,
						 FU_EGIS_MOC_CMD_OTA_WRITE,
						 (guint16)(offset & 0xFFFF),
						 (guint16)((offset >> 16) & 0xFFFF),
						 req->data,
						 req->len,
						 FALSE,
						 error)) {
			g_prefix_error_literal(error, "failed to write: ");
			return FALSE;
		}

		/* update progress */
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_egis_moc_device_write_checksum(FuEgisMocDevice *self, FuChunkArray *chunks, GError **error)
{
	guint8 hmac[FU_EGIS_MOC_HMAC_SHA256_SIZE] = {0};
	g_autoptr(FuChunk) chk = NULL;

	chk = fu_chunk_array_index(chunks, fu_chunk_array_length(chunks) - 1, error);
	if (chk == NULL)
		return FALSE;
	if (!fu_memcpy_safe(hmac,
			    sizeof(hmac),
			    0,
			    fu_chunk_get_data(chk),
			    fu_chunk_get_data_sz(chk),
			    fu_chunk_get_data_sz(chk) - sizeof(hmac),
			    sizeof(hmac),
			    error))
		return FALSE;
	if (!fu_egis_moc_device_ctrl_cmd(self,
					 FU_EGIS_MOC_CMD_OTA_FINAL,
					 0x0,
					 0x0,
					 hmac,
					 sizeof(hmac),
					 FALSE,
					 error)) {
		g_prefix_error_literal(error, "failed to send OTA final: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_egis_moc_device_write_firmware(FuDevice *device,
				  FuFirmware *firmware,
				  FuProgress *progress,
				  FwupdInstallFlags flags,
				  GError **error)
{
	FuEgisMocDevice *self = FU_EGIS_MOC_DEVICE(device);
	g_autoptr(FuChunkArray) chunks = NULL;
	g_autoptr(GBytes) fw = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 99, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, "fini");

	/* get default image */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;

	/* write each block */
	chunks = fu_chunk_array_new_from_bytes(fw,
					       FU_CHUNK_ADDR_OFFSET_NONE,
					       FU_CHUNK_PAGESZ_NONE,
					       FU_EGIS_MOC_FLASH_TRANSFER_BLOCK_SIZE);
	if (!fu_egis_moc_device_write_packets(self, chunks, fu_progress_get_child(progress), error))
		return FALSE;
	fu_progress_step_done(progress);

	/* write checksum */
	if (!fu_egis_moc_device_write_checksum(self, chunks, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success! */
	return TRUE;
}

static gboolean
fu_egis_moc_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuEgisMocDevice *self = FU_EGIS_MOC_DEVICE(device);

	if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_debug("already in runtime mode, skipping");
		return TRUE;
	}

	/* success */
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gboolean
fu_egis_moc_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuEgisMocDevice *self = FU_EGIS_MOC_DEVICE(device);

	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_debug("already in bootloader mode, skipping");
		return TRUE;
	}
	if (!fu_egis_moc_device_update_init(self, error)) {
		g_prefix_error_literal(error, "failed to detach: ");
		return FALSE;
	}

	/* success */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static void
fu_egis_moc_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 7, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 42, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 51, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static gchar *
fu_egis_moc_device_convert_version(FuDevice *device, guint64 version_raw)
{
	return fu_version_from_uint32(version_raw, fu_device_get_version_format(device));
}

static void
fu_egis_moc_device_init(FuEgisMocDevice *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_USE_RUNTIME_VERSION);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_QUAD);
	fu_device_set_remove_delay(FU_DEVICE(self), 10000);
	fu_device_add_protocol(FU_DEVICE(self), "com.egistec.usb");
	fu_device_set_summary(FU_DEVICE(self), "Fingerprint Device");
	fu_device_set_install_duration(FU_DEVICE(self), 15);
	fu_device_set_firmware_size_min(FU_DEVICE(self), 0x20000);
	fu_device_set_firmware_size_max(FU_DEVICE(self), 0x50000);
	fu_usb_device_add_interface(FU_USB_DEVICE(self), FU_EGIS_MOC_USB_INTERFACE);
}

static void
fu_egis_moc_device_class_init(FuEgisMocDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);

	device_class->write_firmware = fu_egis_moc_device_write_firmware;
	device_class->setup = fu_egis_moc_device_setup;
	device_class->reload = fu_egis_moc_device_setup;
	device_class->attach = fu_egis_moc_device_attach;
	device_class->detach = fu_egis_moc_device_detach;
	device_class->set_progress = fu_egis_moc_device_set_progress;
	device_class->convert_version = fu_egis_moc_device_convert_version;
}
