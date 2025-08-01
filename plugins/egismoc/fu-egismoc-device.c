/*
 * Copyright 2025 Jason Huang <jason.huang@egistec.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <mbedtls/md.h>
#include <mbedtls/platform.h>

#include "fu-egismoc-common.h"
#include "fu-egismoc-device.h"

struct _FuEgisMocDevice {
	FuUsbDevice parent_instance;
	guint8 dummy_seq;
};

G_DEFINE_TYPE(FuEgisMocDevice, fu_egismoc_device, FU_TYPE_USB_DEVICE)

#define EGIS_USB_BULK_EP_IN  (1 | 0x80)
#define EGIS_USB_BULK_EP_OUT (2 | 0x00)
#define EGIS_USB_INTERFACE   0

#define FU_USB_REQUEST_RECIPIENT_TO_DEVICE 0
#define FU_USB_REQUEST_TYPE_VENDOR	   2
#define FU_USB_DIRECTION_HOST_TO_DEVICE	   0
#define FU_USB_DIRECTION_DEVICE_TO_HOST	   1

#define EGIS_USB_DATAIN_TIMEOUT	       10000  /* ms */
#define EGIS_USB_DATAOUT_TIMEOUT       10000  /* ms */
#define EGIS_FLASH_TRANSFER_BLOCK_SIZE 0x1000 /* 1000 */

#if 0
static FuFirmware *
fu_egismoc_device_prepare_firmware(FuDevice *device,
			           GInputStream *stream,
				   FuProgress *progress,
				   FuFirmwareParseFlags flags,
				   GError **error)
{
	return
}
#endif

static guint32
fu_egismoc_device_ip_checksum_add(guint32 temp_chksum, const void *data, int len)
{
	guint32 checksum = temp_chksum;
	const guint16 *data16 = (const guint16 *)data;

	while (len > 1) {
		checksum += *data16;
		data16++;
		len -= 2;
	}
	if (len) {
		checksum += *(guint8 *)data16;
	}
	data16 = NULL;
	return checksum;
}

static guint16
fu_egismoc_device_ip_checksum_fold(guint32 temp_chksum)
{
	while (temp_chksum > 0xFFFF) {
		temp_chksum = (temp_chksum >> 16) + (temp_chksum & 0xFFFF);
	}
	return (guint16)temp_chksum;
}

static guint16
fu_egismoc_device_ip_checksum_finish(guint32 temp_chksum)
{
	return ~fu_egismoc_device_ip_checksum_fold(temp_chksum);
}

static gboolean
fu_egismoc_device_ctrl_cmd(FuEgisMocDevice *self,
			   guint8 request,
			   guint16 value,
			   guint16 index,
			   guint8 *data,
			   gsize length,
			   gboolean device2host,
			   GError **error)
{
	gsize actual_len = 0;

	if (!fu_usb_device_control_transfer(FU_USB_DEVICE(self),
					    device2host ? FU_USB_DIRECTION_HOST_TO_DEVICE
							: FU_USB_DIRECTION_DEVICE_TO_HOST,
					    FU_USB_REQUEST_TYPE_VENDOR,
					    FU_USB_REQUEST_RECIPIENT_TO_DEVICE,
					    request,
					    value,
					    index,
					    data,
					    length,
					    length ? &actual_len : NULL,
					    EGIS_USB_DATAOUT_TIMEOUT,
					    NULL,
					    error)) {
		fu_error_convert(error);
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
	return TRUE;
}

static gboolean
fu_egismoc_device_cmd_send(FuEgisMocDevice *self, GByteArray *req, GError **error)
{
	gsize actual_len = 0;
	EgisfpPkgHeader frameheader = {0};
	g_autoptr(GByteArray) buf = g_byte_array_new();
	guint32 temp_chksum = 0;

	/* build header */
	frameheader.Sync[0] = 0x45;
	frameheader.Sync[1] = 0x47;
	frameheader.Sync[2] = 0x49;
	frameheader.Sync[3] = 0x53;

	frameheader.ID[0] = 0x00;
	frameheader.ID[1] = 0x00;
	frameheader.ID[2] = 0x00;
	frameheader.ID[3] = 0x01;

	frameheader.Len[0] = 0x00;
	frameheader.Len[1] = 0x00;
	frameheader.Len[2] = (req->len >> 8) & 0xFF;
	frameheader.Len[3] = (guint8)req->len & 0xFF;

	g_byte_array_append(buf, (guint8 *)&frameheader, sizeof(EgisfpPkgHeader));
	g_byte_array_append(buf, req->data, req->len);
	temp_chksum = fu_egismoc_device_ip_checksum_add(temp_chksum, buf->data, 8);
	temp_chksum = fu_egismoc_device_ip_checksum_add(temp_chksum, buf->data + 10, buf->len - 10);
	((EgisfpPkgHeader *)(buf->data))->ChkSum =
	    fu_egismoc_device_ip_checksum_finish(temp_chksum);

	/* send data */
	if (!fu_usb_device_bulk_transfer(FU_USB_DEVICE(self),
					 EGIS_USB_BULK_EP_OUT,
					 buf->data,
					 buf->len,
					 &actual_len,
					 EGIS_USB_DATAOUT_TIMEOUT,
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
fu_egismoc_device_cmd_recv_cb(FuDevice *self, gpointer user_data, GError **error)
{
	gsize actual_len = 0;
	guint32 temp_chksum = 0;
	guint16 actual_chksum = 0;
	guint16 status = 0;
	GByteArray *presponse = (GByteArray *)user_data;

	/*
	 * package format
	 * | zlp | ack | zlp | data |
	 */
	EgisfpPkgHeader pkgHeader = {0};
	g_autoptr(GByteArray) reply = g_byte_array_new();
	fu_byte_array_set_size(reply, EGIS_FLASH_TRANSFER_BLOCK_SIZE, 0x00);

	if (!fu_usb_device_bulk_transfer(FU_USB_DEVICE(self),
					 EGIS_USB_BULK_EP_IN,
					 reply->data,
					 reply->len,
					 &actual_len, /* allowed to return short read */
					 EGIS_USB_DATAIN_TIMEOUT,
					 NULL,
					 error)) {
		g_prefix_error(error, "failed to reply: ");
		return FALSE;
	}

	if (actual_len == 0)
		return FALSE;

	fu_dump_full(G_LOG_DOMAIN,
		     "REPLY",
		     reply->data,
		     actual_len,
		     16,
		     FU_DUMP_FLAGS_SHOW_ADDRESSES);

	/* parse package header */
	if (!fu_memcpy_safe((guint8 *)&pkgHeader,
			    sizeof(EgisfpPkgHeader),
			    0,
			    reply->data,
			    reply->len,
			    0,
			    sizeof(EgisfpPkgHeader),
			    error))
		return FALSE;

	temp_chksum = fu_egismoc_device_ip_checksum_add(temp_chksum, reply->data, 8);
	temp_chksum =
	    fu_egismoc_device_ip_checksum_add(temp_chksum, reply->data + 10, actual_len - 10);
	actual_chksum = fu_egismoc_device_ip_checksum_finish(temp_chksum);

	if (actual_chksum != pkgHeader.ChkSum) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "invalid checksum, got 0x%x, expected 0x%x",
			    pkgHeader.ChkSum,
			    actual_chksum);
		return FALSE;
	}

	if (!fu_memread_uint16_safe(reply->data,
				    reply->len,
				    actual_len - 2,
				    &status,
				    G_BIG_ENDIAN,
				    error))
		return FALSE;

	if (status != 0x9000) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "status error,0x%x",
			    status);
		return FALSE;
	}

	if (!fu_memcpy_safe(presponse->data,
			    presponse->len,
			    0,
			    reply->data,
			    reply->len,
			    sizeof(EgisfpPkgHeader),
			    actual_len - sizeof(EgisfpPkgHeader) - sizeof(status),
			    error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_egismoc_device_fw_cmd(FuEgisMocDevice *device,
			 GByteArray *req,
			 GByteArray *presponse,
			 GError **error)
{
	FuEgisMocDevice *self = FU_EGISMOC_DEVICE(device);

	if (!fu_egismoc_device_cmd_send(self, req, error))
		return FALSE;

	if (!fu_device_retry(FU_DEVICE(self), fu_egismoc_device_cmd_recv_cb, 10, presponse, error))
		return FALSE;

	return TRUE;
}

static gchar *
fu_egismoc_device_print_to_hex(gpointer buffer, gsize buffer_length)
{
	gpointer ret = g_malloc(buffer_length * 2 + 1);
	gsize i;
	for (i = 0; i < buffer_length; i++) {
		g_snprintf((gchar *)ret + i * 2, 3, "%02x", (guint)(*((guint8 *)buffer + i)));
	}
	return ret;
}

static gboolean
fu_egismoc_device_setup_version(FuEgisMocDevice *self, GError **error)
{
	g_autoptr(GByteArray) rsp = g_byte_array_new();
	EgisCmdReq cmd_data = {0};
	g_autofree gchar *version = NULL;
	g_autoptr(GByteArray) req = g_byte_array_new();

	g_info("enter %s", __func__);

	cmd_data.cla = 0x50;
	cmd_data.ins = 0x7f;
	cmd_data.p1 = 0x00;
	cmd_data.p2 = 0x00;
	cmd_data.lc1 = 0x00;
	cmd_data.lc2 = 0x00;
	cmd_data.lc3 = 0x0C;

	g_byte_array_append(req, (guint8 *)&cmd_data, sizeof(EgisCmdReq));
	fu_byte_array_set_size(rsp, sizeof(EgisfpVersionInfo), 0x00);

	if (!fu_egismoc_device_fw_cmd(self, req, rsp, error))
		return FALSE;

	rsp = g_byte_array_remove_range(rsp, 0, 3);
	version = g_strndup((const gchar *)rsp->data, rsp->len);
	fu_device_set_version(FU_DEVICE(self), version);

	return TRUE;
}

static gboolean
fu_egismoc_device_update_init(FuEgisMocDevice *self, GError **error)
{
	guint8 challenge[OTA_CHALLENGE_SIZE];
	guchar hmac_key[HMAC_SHA256_SIZE] = OTA_CHALLENGE_HMAC_KEY;
	guint8 digest[HMAC_SHA256_SIZE];
	gsize digest_len = HMAC_SHA256_SIZE;
	GHmac *hmac = g_hmac_new(G_CHECKSUM_SHA256, (guchar *)hmac_key, HMAC_SHA256_SIZE);

	g_info("enter %s", __func__);
	/* Get challenge */
	if (!fu_egismoc_device_ctrl_cmd(self,
					0x54,
					0x0,
					0x0,
					challenge,
					OTA_CHALLENGE_SIZE,
					TRUE,
					error)) {
		g_prefix_error(error, "failed to get challenge: ");
		return FALSE;
	}

	g_hmac_update(hmac, (guchar *)challenge, OTA_CHALLENGE_SIZE);
	g_hmac_get_digest(hmac, digest, &digest_len);
	g_hmac_unref(hmac);

	/* Switch into OTA Mode */
	if (!fu_egismoc_device_ctrl_cmd(self,
					0x58,
					0x0,
					0x0,
					digest,
					HMAC_SHA256_SIZE,
					FALSE,
					error)) {
		g_prefix_error(error, "failed to go to OTA mode: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_egismoc_device_setup_mode(FuEgisMocDevice *self, GError **error)
{
	guint8 op_mode[8];

	g_info("enter %s", __func__);

	if (!fu_egismoc_device_ctrl_cmd(self, 0x52, 0x0, 0x0, op_mode, 8, TRUE, error)) {
		g_prefix_error(error, "failed to get mode: ");
		return FALSE;
	}

	if (op_mode[0] == 0x0B) {
		fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	} else {
		fu_device_remove_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	}

	return TRUE;
}

static gboolean
fu_egismoc_device_setup(FuDevice *device, GError **error)
{
	FuEgisMocDevice *self = FU_EGISMOC_DEVICE(device);
	g_autofree gchar *name = NULL;

	g_info("enter %s", __func__);
	/* FuUsbDevice->setup */
	if (!FU_DEVICE_CLASS(fu_egismoc_device_parent_class)->setup(device, error))
		return FALSE;

	name = g_strdup(fu_device_get_name(device));
	if (name != NULL) {
		fu_device_set_name(device, name);
	}

	if (!fu_egismoc_device_setup_mode(self, error)) {
		g_prefix_error(error, "failed to get device mode: ");
		return FALSE;
	}

	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		fu_device_set_version(FU_DEVICE(self), "0.0.0.1");
		return TRUE;
	}
	/* ensure version */
	if (!fu_egismoc_device_setup_version(self, error)) {
		g_prefix_error(error, "failed to get firmware version: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_egismoc_device_write_firmware(FuDevice *device,
				 FuFirmware *firmware,
				 FuProgress *progress,
				 FwupdInstallFlags flags,
				 GError **error)
{
	FuEgisMocDevice *self = FU_EGISMOC_DEVICE(device);

	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(FuChunkArray) chunks = NULL;
	guint32 offset = 0;
	guint8 hmac[HMAC_SHA256_SIZE];

	g_info("enter %s", __func__);

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, "init");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 99, NULL);

	g_info("dfu get default image");
	/* get default image */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;

	/* build packets */
	chunks = fu_chunk_array_new_from_bytes(fw,
					       FU_CHUNK_ADDR_OFFSET_NONE,
					       FU_CHUNK_PAGESZ_NONE,
					       EGIS_FLASH_TRANSFER_BLOCK_SIZE);

#if 0
	/* don't auto-boot firmware */
	if (!fu_egismoc_device_update_init(self, &error_local)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "failed to initial update: %s",
			    error_local->message);
		return FALSE;
	}
#endif
	fu_progress_step_done(progress);

	/* write each block */
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;
		g_autoptr(GByteArray) req = g_byte_array_new();
		g_autoptr(GError) error_block = NULL;
		offset = i * EGIS_FLASH_TRANSFER_BLOCK_SIZE;
		/* prepare chunk */
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		if (i == fu_chunk_array_length(chunks) - 1) {
			g_byte_array_append(req,
					    fu_chunk_get_data(chk),
					    fu_chunk_get_data_sz(chk) - HMAC_SHA256_SIZE);
			if (!fu_memcpy_safe(hmac,
					    HMAC_SHA256_SIZE,
					    0,
					    fu_chunk_get_data(chk),
					    fu_chunk_get_data_sz(chk),
					    fu_chunk_get_data_sz(chk) - HMAC_SHA256_SIZE,
					    HMAC_SHA256_SIZE,
					    error))
				return FALSE;
		} else {
			g_byte_array_append(req, fu_chunk_get_data(chk), fu_chunk_get_data_sz(chk));
		}

		if (!fu_egismoc_device_ctrl_cmd(self,
						0x5A,
						(guint16)(offset & 0xFFFF),
						(guint16)((offset >> 16) & 0xFFFF),
						req->data,
						req->len,
						FALSE,
						&error_block)) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "failed to write: %s",
				    error_block->message);
			return FALSE;
		}

		/* update progress */
		fu_progress_set_percentage_full(fu_progress_get_child(progress),
						(gsize)i + 1,
						(gsize)fu_chunk_array_length(chunks));
	}
	if (!fu_egismoc_device_ctrl_cmd(self,
					0x5C,
					0x0,
					0x0,
					hmac,
					HMAC_SHA256_SIZE,
					FALSE,
					&error_local)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "failed to send OTA final: %s",
			    error_local->message);
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* success! */
	return TRUE;
}

static gboolean
fu_egismoc_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuEgisMocDevice *self = FU_EGISMOC_DEVICE(device);

	g_info("enter %s", __func__);

	if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_debug("already in runtime mode, skipping");
		return TRUE;
	}

	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);

	return TRUE;
}

static gboolean
fu_egismoc_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuEgisMocDevice *self = FU_EGISMOC_DEVICE(device);
	g_autoptr(GError) error_local = NULL;

	g_info("enter %s", __func__);

	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_debug("already in bootloader mode, skipping");
		return TRUE;
	}
	// TODO
	if (!fu_egismoc_device_update_init(self, &error_local)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "failed to detach: %s",
			    error_local->message);
		return FALSE;
	}

	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static void
fu_egismoc_device_set_progress(FuDevice *self, FuProgress *progress)
{
	g_info("enter %s", __func__);
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static gchar *
fu_egismoc_device_convert_version(FuDevice *device, guint64 version_raw)
{
	g_info("enter %s", __func__);
	return fu_version_from_uint32(version_raw, fu_device_get_version_format(device));
}

static void
fu_egismoc_device_init(FuEgisMocDevice *self)
{
	g_info("enter %s", __func__);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SELF_RECOVERY);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_USE_RUNTIME_VERSION);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PLAIN);
	fu_device_set_remove_delay(FU_DEVICE(self), 10000);
	fu_device_add_protocol(FU_DEVICE(self), "com.egistec.usb");
	fu_device_set_summary(FU_DEVICE(self), "Egis MoC Fingerprint Sensor");
	fu_device_set_vendor(FU_DEVICE(self), "Egis");
	fu_device_set_install_duration(FU_DEVICE(self), 15);
	fu_device_set_firmware_size_min(FU_DEVICE(self), 0x20000);
	fu_device_set_firmware_size_max(FU_DEVICE(self), 0x50000);
	fu_usb_device_add_interface(FU_USB_DEVICE(self), EGIS_USB_INTERFACE);
}

static void
fu_egismoc_device_class_init(FuEgisMocDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);

	g_info("enter %s", __func__);

	// device_class->prepare_firmware = fu_egismoc_device_prepare_firmware;
	device_class->write_firmware = fu_egismoc_device_write_firmware;
	device_class->setup = fu_egismoc_device_setup;
	device_class->reload = fu_egismoc_device_setup;
	device_class->attach = fu_egismoc_device_attach;
	device_class->detach = fu_egismoc_device_detach;
	device_class->set_progress = fu_egismoc_device_set_progress;
	device_class->convert_version = fu_egismoc_device_convert_version;
}
