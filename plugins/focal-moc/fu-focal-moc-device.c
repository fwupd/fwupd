/*
 * Copyright 2024 FocalTech Systems Co., Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-focal-moc-common.h"
#include "fu-focal-moc-device.h"
#include "fu-focal-moc-struct.h"

struct _FuFocalMocDevice {
	FuUsbDevice parent_instance;
};

G_DEFINE_TYPE(FuFocalMocDevice, fu_focal_moc_device, FU_TYPE_USB_DEVICE)

/* USB bulk transfer endpoints */
#define FU_FOCAL_MOC_USB_EP_IN	   (1 | 0x80)
#define FU_FOCAL_MOC_USB_EP_OUT	   (2 | 0x00)
#define FU_FOCAL_MOC_USB_INTERFACE 0

#define FU_FOCAL_MOC_USB_TIMEOUT	1000 /* ms — send */
#define FU_FOCAL_MOC_RECV_TIMEOUT	1000 /* ms — receive */
#define FU_FOCAL_MOC_RECV_FINAL_TIMEOUT 2000 /* ms — longer wait after EOT */

/* firmware-download packet constants */
#define FU_FOCAL_MOC_DL_BLOCK_SIZE 1024		 /* bytes per STX/EOT data block */
#define FU_FOCAL_MOC_FW_MAX_SIZE   (256 * FU_KB) /* hard limit */

/* transfer timing delays */
#define FU_FOCAL_MOC_DELAY_CMD_MS   5	/* after version/mode commands */
#define FU_FOCAL_MOC_DELAY_DL_MS    10	/* between download packets */
#define FU_FOCAL_MOC_DELAY_FINAL_MS 200 /* before reading final ACK */

/* maximum receive buffer (covers any response packet) */
#define FU_FOCAL_MOC_MAX_RSP_SIZE 256

/* the emulation event key includes the requested transfer size, so the version
 * reply has to keep asking for the smaller size it was recorded with */
#define FU_FOCAL_MOC_VERSION_RSP_SIZE 64

static gboolean
fu_focal_moc_device_send(FuFocalMocDevice *self, GByteArray *pkt, GError **error)
{
	gsize actual = 0;

	fu_dump_full(G_LOG_DOMAIN, "SEND", pkt->data, pkt->len, 16, FU_DUMP_FLAG_SHOW_ADDRESSES);

	if (!fu_usb_device_bulk_transfer(FU_USB_DEVICE(self),
					 FU_FOCAL_MOC_USB_EP_OUT,
					 pkt->data,
					 pkt->len,
					 &actual,
					 FU_FOCAL_MOC_USB_TIMEOUT,
					 NULL,
					 error)) {
		g_prefix_error_literal(error, "send failed: ");
		return FALSE;
	}
	if (actual != pkt->len) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "short write: sent %zu of %u bytes",
			    actual,
			    pkt->len);
		return FALSE;
	}

	/* success */
	return TRUE;
}

/* read one response packet, verify it is an ACK and return any payload */
static GByteArray *
fu_focal_moc_device_recv(FuFocalMocDevice *self, guint timeout_ms, gsize bufsz, GError **error)
{
	gsize actual = 0;
	guint8 buf[FU_FOCAL_MOC_MAX_RSP_SIZE] = {0};

	g_return_val_if_fail(bufsz <= sizeof(buf), NULL);

	if (!fu_usb_device_bulk_transfer(FU_USB_DEVICE(self),
					 FU_FOCAL_MOC_USB_EP_IN,
					 buf,
					 bufsz,
					 &actual,
					 timeout_ms,
					 NULL,
					 error)) {
		g_prefix_error_literal(error, "recv failed: ");
		return NULL;
	}
	fu_dump_full(G_LOG_DOMAIN, "RECV", buf, actual, 16, FU_DUMP_FLAG_SHOW_ADDRESSES);
	return fu_focal_moc_packet_parse(buf, actual, error);
}

static gboolean
fu_focal_moc_device_recv_ack(FuFocalMocDevice *self, guint timeout_ms, GError **error)
{
	g_autoptr(GByteArray) data = NULL;
	data = fu_focal_moc_device_recv(self, timeout_ms, FU_FOCAL_MOC_MAX_RSP_SIZE, error);
	return data != NULL;
}

/* send a standard command packet and receive the ACK */
static gboolean
fu_focal_moc_device_cmd_xfer(FuFocalMocDevice *self,
			     FuFocalMocCmd cmd,
			     const guint8 *buf,
			     gsize buf_len,
			     guint delay_ms,
			     GError **error)
{
	g_autoptr(GByteArray) pkt = fu_focal_moc_packet_new(cmd, buf, buf_len, error);

	if (pkt == NULL)
		return FALSE;
	if (!fu_focal_moc_device_send(self, pkt, error))
		return FALSE;

	fu_device_sleep(FU_DEVICE(self), delay_ms);

	return fu_focal_moc_device_recv_ack(self, FU_FOCAL_MOC_RECV_TIMEOUT, error);
}

static gboolean
fu_focal_moc_device_ensure_version(FuFocalMocDevice *self, GError **error)
{
	gboolean is_bootloader = FALSE;
	g_autofree gchar *version = NULL;
	g_autoptr(GByteArray) data = NULL;
	g_autoptr(GByteArray) pkt = NULL;

	pkt = fu_focal_moc_packet_new(FU_FOCAL_MOC_CMD_GET_FW_VERSION, NULL, 0, error);
	if (pkt == NULL)
		return FALSE;
	if (!fu_focal_moc_device_send(self, pkt, error)) {
		g_prefix_error_literal(error, "version: ");
		return FALSE;
	}
	fu_device_sleep(FU_DEVICE(self), FU_FOCAL_MOC_DELAY_CMD_MS);
	data = fu_focal_moc_device_recv(self,
					FU_FOCAL_MOC_RECV_TIMEOUT,
					FU_FOCAL_MOC_VERSION_RSP_SIZE,
					error);
	if (data == NULL) {
		g_prefix_error_literal(error, "version recv: ");
		return FALSE;
	}

	/* e.g. "FT9769DL_APP_FT9001_USB_DEC_SV0.1_7396" */
	version = fu_focal_moc_version_parse(data->data, data->len, &is_bootloader, error);
	if (version == NULL)
		return FALSE;
	fu_device_set_version(FU_DEVICE(self), version);
	if (is_bootloader) {
		fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	} else {
		fu_device_remove_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	}

	/* success */
	return TRUE;
}

/*
 * fu_focal_moc_device_set_boot_mode:
 *
 * CMD 0x32 — set device boot mode
 *
 * Packet: [ 0x02 | 0x00 0x02 | 0x32 | mode | BCC ]
 *   LEN = 2  (1 data byte + 1 BCC)
 */
static gboolean
fu_focal_moc_device_set_boot_mode(FuFocalMocDevice *self, FuFocalMocBootMode mode, GError **error)
{
	guint8 buf = (guint8)mode;
	return fu_focal_moc_device_cmd_xfer(self,
					    FU_FOCAL_MOC_CMD_SET_BOOT_MODE,
					    &buf,
					    1,
					    FU_FOCAL_MOC_DELAY_CMD_MS,
					    error);
}

static gboolean
fu_focal_moc_device_dl_pkt(FuFocalMocDevice *self,
			   FuFocalMocMagic magic,
			   guint8 seq,
			   const guint8 *data,
			   gsize data_len,
			   GError **error)
{
	guint8 bcc;
	g_autoptr(FuStructFocalMocDlHdr) st_req = fu_struct_focal_moc_dl_hdr_new();

	fu_struct_focal_moc_dl_hdr_set_ln(st_req, (guint16)(3 + data_len));
	fu_struct_focal_moc_dl_hdr_set_cmd(st_req, FU_FOCAL_MOC_CMD_FW_DOWNLOAD);
	fu_struct_focal_moc_dl_hdr_set_magic(st_req, magic);
	fu_struct_focal_moc_dl_hdr_set_seq(st_req, seq);
	if (data != NULL && data_len > 0)
		g_byte_array_append(st_req->buf, data, data_len);
	bcc = fu_xor8(st_req->buf->data + 1, st_req->buf->len - 1);
	fu_byte_array_append_uint8(st_req->buf, bcc);

	return fu_focal_moc_device_send(self, st_req->buf, error);
}

static gboolean
fu_focal_moc_device_setup(FuDevice *device, GError **error)
{
	FuFocalMocDevice *self = FU_FOCAL_MOC_DEVICE(device);

	if (!FU_DEVICE_CLASS(fu_focal_moc_device_parent_class)->setup(device, error))
		return FALSE;

	if (!fu_focal_moc_device_ensure_version(self, error)) {
		g_prefix_error_literal(error, "failed to read firmware version: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_focal_moc_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuFocalMocDevice *self = FU_FOCAL_MOC_DEVICE(device);

	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_debug("already in bootloader mode, skipping");
		return TRUE;
	}
	if (!fu_focal_moc_device_set_boot_mode(self, FU_FOCAL_MOC_BOOT_MODE_ENTER_BOOT, error)) {
		g_prefix_error_literal(error, "failed to enter bootloader mode: ");
		return FALSE;
	}

	/* success */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gboolean
fu_focal_moc_device_write_sho(FuFocalMocDevice *self,
			      FuInputStream *stream,
			      guint8 *seq,
			      GError **error)
{
	guint32 crc32 = G_MAXUINT32;
	gsize streamsz = 0;
	g_autoptr(FuStructFocalMocShoData) st_sho = fu_struct_focal_moc_sho_data_new();

	if (!fu_struct_focal_moc_sho_data_set_filename(st_sho, "firmware.bin", error))
		return FALSE;
	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	fu_struct_focal_moc_sho_data_set_filesize(st_sho, (guint32)streamsz);
	if (!fu_input_stream_compute_crc32(stream, FU_CRC_KIND_B32_STANDARD, &crc32, error))
		return FALSE;
	fu_struct_focal_moc_sho_data_set_crc32(st_sho, crc32);

	if (!fu_focal_moc_device_dl_pkt(self,
					FU_FOCAL_MOC_MAGIC_SHO,
					(*seq)++,
					st_sho->buf->data,
					st_sho->buf->len,
					error)) {
		g_prefix_error_literal(error, "SHO packet send failed: ");
		return FALSE;
	}
	fu_device_sleep(FU_DEVICE(self), FU_FOCAL_MOC_DELAY_DL_MS);
	if (!fu_focal_moc_device_recv_ack(self, FU_FOCAL_MOC_RECV_TIMEOUT, error)) {
		g_prefix_error_literal(error, "SHO ACK failed: ");
		return FALSE;
	}

	/* success; short settle after SHO ACK */
	fu_device_sleep(FU_DEVICE(self), 15);
	return TRUE;
}

static gboolean
fu_focal_moc_device_write_stx_chunk(FuFocalMocDevice *self,
				    FuChunk *chk,
				    guint8 *seq,
				    GError **error)
{
	if (!fu_focal_moc_device_dl_pkt(self,
					FU_FOCAL_MOC_MAGIC_STX,
					(*seq)++,
					fu_chunk_get_data(chk),
					fu_chunk_get_data_sz(chk),
					error)) {
		g_prefix_error(error, "block %u send failed: ", fu_chunk_get_idx(chk));
		return FALSE;
	}

	/* collect per-block ACK */
	fu_device_sleep(FU_DEVICE(self), FU_FOCAL_MOC_DELAY_DL_MS);
	if (!fu_focal_moc_device_recv_ack(self, FU_FOCAL_MOC_RECV_TIMEOUT, error)) {
		g_prefix_error(error, "block %u ACK failed: ", fu_chunk_get_idx(chk));
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_focal_moc_device_write_eot_chunk(FuFocalMocDevice *self,
				    FuChunk *chk,
				    guint8 *seq,
				    GError **error)
{
	g_autoptr(GByteArray) tail_buf = g_byte_array_new();
	g_byte_array_append(tail_buf, fu_chunk_get_data(chk), fu_chunk_get_data_sz(chk));
	fu_byte_array_set_size(tail_buf, FU_FOCAL_MOC_DL_BLOCK_SIZE, 0x0);
	if (!fu_focal_moc_device_dl_pkt(self,
					FU_FOCAL_MOC_MAGIC_EOT,
					(*seq)++,
					tail_buf->data,
					tail_buf->len,
					error)) {
		g_prefix_error_literal(error, "EOT packet send failed: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_focal_moc_device_write_stx_chunks(FuFocalMocDevice *self,
				     FuChunkArray *chunks,
				     guint8 *seq,
				     FuProgress *progress,
				     GError **error)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));

	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		if (i < fu_chunk_array_length(chunks) - 1) {
			if (!fu_focal_moc_device_write_stx_chunk(self, chk, seq, error))
				return FALSE;
		} else {
			if (!fu_focal_moc_device_write_eot_chunk(self, chk, seq, error))
				return FALSE;
		}
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_focal_moc_device_write_final_ack(FuFocalMocDevice *self, GError **error)
{
	fu_device_sleep(FU_DEVICE(self), FU_FOCAL_MOC_DELAY_FINAL_MS);
	if (!fu_focal_moc_device_recv_ack(self, FU_FOCAL_MOC_RECV_FINAL_TIMEOUT, error)) {
		g_prefix_error_literal(error, "final ACK failed: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_focal_moc_device_write_firmware(FuDevice *device,
				   FuFirmware *firmware,
				   FuProgress *progress,
				   FwupdInstallFlags flags,
				   GError **error)
{
	FuFocalMocDevice *self = FU_FOCAL_MOC_DEVICE(device);
	guint8 seq = 0;
	g_autoptr(FuInputStream) stream = NULL;
	g_autoptr(FuChunkArray) chunks = NULL;

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 1, "sho");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 98, "chunks");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 1, "final-ack");

	stream = fu_firmware_get_stream(firmware, error);
	if (stream == NULL)
		return FALSE;

	/* send header packet with filename, file size, CRC32; then wait for ACK */
	if (!fu_focal_moc_device_write_sho(self, stream, &seq, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* send 1024-byte data blocks; wait for ACK after each */
	chunks = fu_chunk_array_new_from_stream(stream,
						FU_CHUNK_ADDR_OFFSET_NONE,
						FU_CHUNK_PAGESZ_NONE,
						FU_FOCAL_MOC_DL_BLOCK_SIZE,
						error);
	if (chunks == NULL)
		return FALSE;
	if (!fu_focal_moc_device_write_stx_chunks(self,
						  chunks,
						  &seq,
						  fu_progress_get_child(progress),
						  error))
		return FALSE;
	fu_progress_step_done(progress);

	/* send remaining (<1024) bytes; wait for final ACK */
	if (!fu_focal_moc_device_write_final_ack(self, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static gboolean
fu_focal_moc_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuFocalMocDevice *self = FU_FOCAL_MOC_DEVICE(device);
	g_autoptr(GError) error_local = NULL;

	/* some firmware images trigger an automatic reboot after the EOT+ACK;
	 * others (e.g. test payloads) do not; sending ENTER_APP explicitly covers both cases */
	if (!fu_focal_moc_device_set_boot_mode(self,
					       FU_FOCAL_MOC_BOOT_MODE_ENTER_APP,
					       &error_local)) {
		g_debug("ENTER_APP command failed (device may have already rebooted): %s",
			error_local->message);
	}

	/* success */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static void
fu_focal_moc_device_set_progress(FuDevice *device, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 2, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 74, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 24, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static void
fu_focal_moc_device_init(FuFocalMocDevice *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SELF_RECOVERY);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INSTALL_SKIP_VERSION_CHECK);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_ENSURE_SEMVER);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_RETRY_OPEN);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_USE_RUNTIME_VERSION);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PLAIN);
	fu_device_set_remove_delay(FU_DEVICE(self), 10000);
	fu_device_add_protocol(FU_DEVICE(self), "com.focal.moc");
	fu_device_set_name(FU_DEVICE(self), "Fingerprint Sensor");
	fu_device_set_summary(FU_DEVICE(self), "Match-On-Chip fingerprint sensor");
	fu_device_set_install_duration(FU_DEVICE(self), 15);
	fu_device_set_firmware_size_min(FU_DEVICE(self), 2 * FU_FOCAL_MOC_DL_BLOCK_SIZE);
	fu_device_set_firmware_size_max(FU_DEVICE(self), FU_FOCAL_MOC_FW_MAX_SIZE);
	fu_usb_device_add_interface(FU_USB_DEVICE(self), FU_FOCAL_MOC_USB_INTERFACE);
}

static void
fu_focal_moc_device_class_init(FuFocalMocDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->setup = fu_focal_moc_device_setup;
	device_class->detach = fu_focal_moc_device_detach;
	device_class->write_firmware = fu_focal_moc_device_write_firmware;
	device_class->attach = fu_focal_moc_device_attach;
	device_class->set_progress = fu_focal_moc_device_set_progress;
}
