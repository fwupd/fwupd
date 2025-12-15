/*
 * Copyright 2025 Harris Tai <harris_tai@pixart.com>
 * Copyright 2025 Micky Hsieh <micky_hsieh@pixart.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-pxi-tp-register.h"
#include "fu-pxi-tp-struct.h"
#include "fu-pxi-tp-tf-communication.h"

typedef struct {
	guint8 mode;
	guint8 *version; /* len = FU_PXI_TF_PAYLOAD_SIZE_VERSION */
} FuPxiTpTfReadFwVersionCtx;

typedef struct {
	guint8 *status;
	guint16 *packet_number;
} FuPxiTpTfReadDlStatusCtx;

typedef struct {
	guint32 packet_total;
	guint32 packet_index;
	const guint8 *chunk_data;
	gsize chunk_len;
} FuPxiTpTfWritePacketCtx;

static gboolean
fu_pxi_tp_tf_communication_write_rmi_cmd(FuPxiTpDevice *self,
					 guint16 addr,
					 const guint8 *in_buf,
					 gsize in_bufsz,
					 GError **error)
{
	const gsize crc_off = (gsize)FU_PXI_TF_FRAME_OFFSET_CRC_START;
	g_autoptr(FuStructPxiTfWriteSimpleCmd) st = NULL;
	guint8 crc;
	gsize need;

	st = fu_struct_pxi_tf_write_simple_cmd_new();
	if (st == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "failed to allocate tf write simple cmd");
		return FALSE;
	}

	fu_struct_pxi_tf_write_simple_cmd_set_addr(st, addr);
	fu_struct_pxi_tf_write_simple_cmd_set_len(st, (guint16)in_bufsz);

	if (in_bufsz > 0 && in_buf != NULL)
		g_byte_array_append(st->buf, in_buf, (guint)in_bufsz);

	crc = fu_crc8(FU_CRC_KIND_B8_STANDARD, st->buf->data + crc_off, st->buf->len - crc_off);
	g_byte_array_append(st->buf, &crc, 1);
	g_byte_array_append(st->buf, (const guint8 *)&(guint8){FU_PXI_TF_FRAME_CONST_TAIL}, 1);

	need = FU_PXI_TF_FRAME_SIZE_FEATURE_REPORT_LEN - st->buf->len;
	if (need > 0)
		g_byte_array_set_size(st->buf, FU_PXI_TF_FRAME_SIZE_FEATURE_REPORT_LEN);

	return fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(self),
					    st->buf->data,
					    st->buf->len,
					    FU_IOCTL_FLAG_NONE,
					    error);
}

static gboolean
fu_pxi_tp_tf_communication_write_rmi_with_packet(FuPxiTpDevice *self,
						 guint16 addr,
						 gsize packet_total,
						 gsize packet_index,
						 const guint8 *in_buf,
						 gsize in_bufsz,
						 GError **error)
{
	const gsize crc_off = (gsize)FU_PXI_TF_FRAME_OFFSET_CRC_START;
	g_autoptr(FuStructPxiTfWritePacketCmd) st = NULL;
	const gsize payload_overhead = sizeof(guint16) * 2;
	guint8 crc;
	gsize datalen;
	gsize need;

	st = fu_struct_pxi_tf_write_packet_cmd_new();
	if (st == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "failed to allocate tf write packet cmd");
		return FALSE;
	}

	/* protocol overhead: packet_total (2) + packet_index (2) */
	datalen = in_bufsz + payload_overhead;

	fu_struct_pxi_tf_write_packet_cmd_set_addr(st, addr);
	fu_struct_pxi_tf_write_packet_cmd_set_datalen(st, (guint16)datalen);
	fu_struct_pxi_tf_write_packet_cmd_set_packet_total(st, (guint16)packet_total);
	fu_struct_pxi_tf_write_packet_cmd_set_packet_index(st, (guint16)packet_index);

	if (in_bufsz > 0 && in_buf != NULL)
		g_byte_array_append(st->buf, in_buf, (guint)in_bufsz);

	crc = fu_crc8(FU_CRC_KIND_B8_STANDARD, st->buf->data + crc_off, st->buf->len - crc_off);
	g_byte_array_append(st->buf, &crc, 1);
	g_byte_array_append(st->buf, (const guint8 *)&(guint8){FU_PXI_TF_FRAME_CONST_TAIL}, 1);

	need = FU_PXI_TF_FRAME_SIZE_FEATURE_REPORT_LEN - st->buf->len;
	if (need > 0)
		g_byte_array_set_size(st->buf, FU_PXI_TF_FRAME_SIZE_FEATURE_REPORT_LEN);

	return fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(self),
					    st->buf->data,
					    st->buf->len,
					    FU_IOCTL_FLAG_NONE,
					    error);
}

static gboolean
fu_pxi_tp_tf_communication_read_rmi(FuPxiTpDevice *self,
				    guint16 addr,
				    const guint8 *in_buf,
				    gsize in_bufsz,
				    guint8 *io_buf,
				    gsize io_bufsz,
				    gsize *n_bytes_returned,
				    GError **error)
{
	g_autoptr(FuStructPxiTfReadCmd) st_read = NULL;
	g_autoptr(FuStructPxiTfReplyHdr) st_hdr = NULL;
	gsize offset;
	gsize datalen;
	guint8 crc;

	g_return_val_if_fail(io_buf != NULL, FALSE);
	g_return_val_if_fail(io_bufsz >= (gsize)FU_PXI_TF_FRAME_SIZE_FEATURE_REPORT_LEN, FALSE);

	memset(io_buf, 0, io_bufsz);

	st_read = fu_struct_pxi_tf_read_cmd_new();
	if (st_read == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "failed to allocate tf read cmd");
		return FALSE;
	}

	/* datalen = input length + 2 bytes reply length (low/high) */
	datalen = in_bufsz + sizeof(guint16);

	fu_struct_pxi_tf_read_cmd_set_addr(st_read, addr);
	fu_struct_pxi_tf_read_cmd_set_datalen(st_read, (guint16)datalen);

	if (n_bytes_returned != NULL) {
		gsize hint = *n_bytes_returned;
		if (hint > G_MAXUINT16)
			hint = G_MAXUINT16;
		fu_struct_pxi_tf_read_cmd_set_reply_len(st_read, (guint16)hint);
	} else {
		fu_struct_pxi_tf_read_cmd_set_reply_len(st_read, 0);
	}

	/* copy header */
	if (!fu_memcpy_safe(io_buf,
			    io_bufsz,
			    0,
			    st_read->buf->data,
			    st_read->buf->len,
			    0,
			    st_read->buf->len,
			    error))
		return FALSE;

	offset = FU_STRUCT_PXI_TF_READ_CMD_SIZE;

	/* append payload (optional) */
	if (in_bufsz > 0 && in_buf != NULL) {
		if (!fu_memcpy_safe(io_buf, io_bufsz, offset, in_buf, in_bufsz, 0, in_bufsz, error))
			return FALSE;
		offset += in_bufsz;
	}

	/* append crc + tail */
	crc = fu_crc8(FU_CRC_KIND_B8_STANDARD,
		      io_buf + (gsize)FU_PXI_TF_FRAME_OFFSET_CRC_START,
		      offset - (gsize)FU_PXI_TF_FRAME_OFFSET_CRC_START);
	io_buf[offset++] = crc;
	io_buf[offset++] = FU_PXI_TF_FRAME_CONST_TAIL;

	if (!fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(self),
					  io_buf,
					  (gsize)FU_PXI_TF_FRAME_SIZE_FEATURE_REPORT_LEN,
					  FU_IOCTL_FLAG_NONE,
					  error))
		return FALSE;

	fu_device_sleep(FU_DEVICE(self), (guint)FU_PXI_TF_TIMING_RMI_REPLY_WAIT);

	if (!fu_hidraw_device_get_feature(FU_HIDRAW_DEVICE(self),
					  io_buf,
					  (gsize)FU_PXI_TF_FRAME_SIZE_FEATURE_REPORT_LEN,
					  FU_IOCTL_FLAG_NONE,
					  error))
		return FALSE;

	/* parse reply header */
	st_hdr = fu_struct_pxi_tf_reply_hdr_parse(io_buf,
						  (gsize)FU_PXI_TF_FRAME_SIZE_FEATURE_REPORT_LEN,
						  0,
						  error);
	if (st_hdr == NULL)
		return FALSE;

	if (fu_struct_pxi_tf_reply_hdr_get_preamble(st_hdr) != FU_PXI_TF_FRAME_CONST_PREAMBLE ||
	    fu_struct_pxi_tf_reply_hdr_get_target_addr(st_hdr) != FU_PXI_TF_TARGET_ADDR_RMI_FRAME) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "tf rmi read: invalid header 0x%02x 0x%02x",
			    fu_struct_pxi_tf_reply_hdr_get_preamble(st_hdr),
			    fu_struct_pxi_tf_reply_hdr_get_target_addr(st_hdr));
		return FALSE;
	}

	/* exception frame? */
	if ((fu_struct_pxi_tf_reply_hdr_get_func(st_hdr) & FU_PXI_TF_FRAME_CONST_EXCEPTION_FLAG) !=
	    0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "tf rmi read: device returned exception 0x%02x",
			    fu_struct_pxi_tf_reply_hdr_get_func(st_hdr));
		return FALSE;
	}

	datalen = (gsize)fu_struct_pxi_tf_reply_hdr_get_datalen(st_hdr);

	/* validate crc + tail */
	{
		const gsize hdr_sz = (gsize)FU_PXI_TF_REPLY_LAYOUT_REPLY_HDR_BYTES;
		const gsize trailer_sz = (gsize)FU_PXI_TF_REPLY_LAYOUT_TRAILER_BYTES;
		const gsize crc_bytes = (gsize)FU_PXI_TF_FRAME_SIZE_CRC_BYTES;
		const gsize crc_idx = hdr_sz + datalen;
		const gsize tail_idx = crc_idx + crc_bytes;
		const gsize frame_sz = hdr_sz + datalen + trailer_sz;
		const gsize crc_len = (hdr_sz - (gsize)FU_PXI_TF_FRAME_OFFSET_CRC_START) + datalen;

		if (frame_sz > (gsize)FU_PXI_TF_FRAME_SIZE_FEATURE_REPORT_LEN) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_WRITE,
					    "tf rmi read: frame exceeds feature report size");
			return FALSE;
		}

		if (fu_crc8(FU_CRC_KIND_B8_STANDARD,
			    io_buf + (gsize)FU_PXI_TF_FRAME_OFFSET_CRC_START,
			    crc_len) != io_buf[crc_idx] ||
		    io_buf[tail_idx] != FU_PXI_TF_FRAME_CONST_TAIL) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_WRITE,
					    "tf rmi read: crc or tail mismatch");
			return FALSE;
		}

		if (n_bytes_returned != NULL)
			*n_bytes_returned = frame_sz;
	}

	return TRUE;
}

/* fu_device_retry_full() callbacks */

static gboolean
fu_pxi_tp_tf_communication_read_firmware_version_cb(FuDevice *device,
						    gpointer user_data,
						    GError **error)
{
	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(device);
	FuPxiTpTfReadFwVersionCtx *ctx = (FuPxiTpTfReadFwVersionCtx *)user_data;
	guint8 io_buf[FU_PXI_TF_FRAME_SIZE_FEATURE_REPORT_LEN] = {0};
	gsize len = FU_PXI_TF_PAYLOAD_SIZE_VERSION;
	g_autoptr(FuStructPxiTfVersionPayload) st_ver = NULL;

	if (!fu_pxi_tp_tf_communication_read_rmi(self,
						 FU_PXI_TF_CMD_READ_VERSION,
						 &ctx->mode,
						 1,
						 io_buf,
						 sizeof(io_buf),
						 &len,
						 error))
		return FALSE;

	st_ver = fu_struct_pxi_tf_version_payload_parse(io_buf,
							sizeof(io_buf),
							FU_STRUCT_PXI_TF_REPLY_HDR_SIZE,
							error);
	if (st_ver == NULL)
		return FALSE;

	ctx->version[0] = fu_struct_pxi_tf_version_payload_get_major(st_ver);
	ctx->version[1] = fu_struct_pxi_tf_version_payload_get_minor(st_ver);
	ctx->version[2] = fu_struct_pxi_tf_version_payload_get_patch(st_ver);

	return TRUE;
}

static gboolean
fu_pxi_tp_tf_communication_read_download_status_cb(FuDevice *device,
						   gpointer user_data,
						   GError **error)
{
	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(device);
	FuPxiTpTfReadDlStatusCtx *ctx = (FuPxiTpTfReadDlStatusCtx *)user_data;
	guint8 io_buf[FU_PXI_TF_FRAME_SIZE_FEATURE_REPORT_LEN] = {0};
	gsize len = FU_PXI_TF_PAYLOAD_SIZE_DOWNLOAD_STATUS;
	g_autoptr(FuStructPxiTfDownloadStatusPayload) st_st = NULL;

	if (!fu_pxi_tp_tf_communication_read_rmi(self,
						 FU_PXI_TF_CMD_READ_UPGRADE_STATUS,
						 NULL,
						 0,
						 io_buf,
						 sizeof(io_buf),
						 &len,
						 error))
		return FALSE;

	/* hdr + payload + trailer */
	{
		const gsize hdr_sz = (gsize)FU_PXI_TF_REPLY_LAYOUT_REPLY_HDR_BYTES;
		const gsize trailer_sz = (gsize)FU_PXI_TF_REPLY_LAYOUT_TRAILER_BYTES;
		const gsize min_len =
		    hdr_sz + (gsize)FU_PXI_TF_PAYLOAD_SIZE_DOWNLOAD_STATUS + trailer_sz;
		if (len < min_len) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "download status reply has unexpected length: %" G_GSIZE_FORMAT,
				    len);
			return FALSE;
		}
	}

	st_st = fu_struct_pxi_tf_download_status_payload_parse(io_buf,
							       sizeof(io_buf),
							       FU_STRUCT_PXI_TF_REPLY_HDR_SIZE,
							       error);
	if (st_st == NULL)
		return FALSE;

	*(ctx->status) = fu_struct_pxi_tf_download_status_payload_get_status(st_st);
	*(ctx->packet_number) =
	    (guint16)fu_struct_pxi_tf_download_status_payload_get_packet_number(st_st);

	return TRUE;
}

static gboolean
fu_pxi_tp_tf_communication_write_packet_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(device);
	FuPxiTpTfWritePacketCtx *ctx = (FuPxiTpTfWritePacketCtx *)user_data;

	return fu_pxi_tp_tf_communication_write_rmi_with_packet(self,
								FU_PXI_TF_CMD_WRITE_UPGRADE_DATA,
								(gsize)ctx->packet_total,
								(gsize)ctx->packet_index,
								ctx->chunk_data,
								ctx->chunk_len,
								error);
}

/* mode: 1=APP, 2=BOOT, 3=ALGO */
gboolean
fu_pxi_tp_tf_communication_read_firmware_version(FuPxiTpDevice *self,
						 guint8 mode,
						 guint8 *version,
						 GError **error)
{
	FuPxiTpTfReadFwVersionCtx ctx;

	ctx.mode = mode;
	ctx.version = version;

	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_pxi_tp_tf_communication_read_firmware_version_cb,
				  (guint)FU_PXI_TF_RETRY_TIMES,
				  (guint)FU_PXI_TF_RETRY_INTERVAL_MS,
				  &ctx,
				  error)) {
		if (error != NULL && *error != NULL)
			g_prefix_error_literal(error, "failed to read firmware version: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_pxi_tp_tf_communication_read_download_status(FuPxiTpDevice *self,
						guint8 *status,
						guint16 *packet_number,
						GError **error)
{
	FuPxiTpTfReadDlStatusCtx ctx;

	ctx.status = status;
	ctx.packet_number = packet_number;

	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_pxi_tp_tf_communication_read_download_status_cb,
				  (guint)FU_PXI_TF_RETRY_TIMES,
				  (guint)FU_PXI_TF_RETRY_INTERVAL_MS,
				  &ctx,
				  error)) {
		if (error != NULL && *error != NULL)
			g_prefix_error_literal(error, "failed to read download status: ");
		return FALSE;
	}

	return TRUE;
}

gboolean
fu_pxi_tp_tf_communication_exit_upgrade_mode(FuPxiTpDevice *self, GError **error)
{
	guint8 mode = FU_PXI_TF_UPGRADE_MODE_EXIT;

	return fu_pxi_tp_tf_communication_write_rmi_cmd(self,
							FU_PXI_TF_CMD_SET_UPGRADE_MODE,
							&mode,
							1,
							error);
}

static gboolean
fu_pxi_tp_tf_communication_write_firmware(FuPxiTpDevice *self,
					  FuProgress *progress,
					  guint32 send_interval,
					  guint32 data_size,
					  const GByteArray *data,
					  GError **error)
{
	guint8 touch_operate_buf;
	guint8 tmp_buf[FU_PXI_TF_FRAME_SIZE_FEATURE_REPORT_LEN] = {0};
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(FuChunkArray) chunks = NULL;
	gsize num_chunks;
	guint32 num;
	guint32 idx;

	touch_operate_buf = FU_PXI_TF_TOUCH_CONTROL_DISABLE;
	memset(tmp_buf, 0, sizeof(tmp_buf));

	g_debug("disabling touch");
	if (!fu_pxi_tp_tf_communication_write_rmi_cmd(self,
						      FU_PXI_TF_CMD_TOUCH_CONTROL,
						      &touch_operate_buf,
						      1,
						      error)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "failed to disable touch");
		return FALSE;
	}

	g_debug("enter bootloader mode");
	tmp_buf[0] = FU_PXI_TF_UPGRADE_MODE_ENTER_BOOT;
	if (!fu_pxi_tp_tf_communication_write_rmi_cmd(self,
						      FU_PXI_TF_CMD_SET_UPGRADE_MODE,
						      tmp_buf,
						      1,
						      error)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "failed to enter bootloader mode");
		return FALSE;
	}

	fu_device_sleep(FU_DEVICE(self), (guint)FU_PXI_TF_TIMING_BOOTLOADER_ENTER_WAIT);

	g_debug("erase flash");
	tmp_buf[0] = FU_PXI_TF_UPGRADE_MODE_ERASE_FLASH;
	if (!fu_pxi_tp_tf_communication_write_rmi_cmd(self,
						      FU_PXI_TF_CMD_SET_UPGRADE_MODE,
						      tmp_buf,
						      1,
						      error)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "failed to send erase flash command");
		return FALSE;
	}

	fu_device_sleep(FU_DEVICE(self), (guint)FU_PXI_TF_TIMING_ERASE_WAIT);

	blob = g_bytes_new(data->data, data_size);
	chunks = fu_chunk_array_new_from_bytes(blob,
					       FU_CHUNK_ADDR_OFFSET_NONE,
					       FU_CHUNK_PAGESZ_NONE,
					       (gsize)FU_PXI_TF_LIMIT_MAX_PACKET_DATA_LEN);
	if (chunks == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "failed to create chunk array from firmware data");
		return FALSE;
	}

	num_chunks = fu_chunk_array_length(chunks);
	if (num_chunks == 0) {
		g_debug("no firmware data to write (zero chunks)");
		return TRUE;
	}

	num = (guint32)num_chunks;

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_set_steps(progress, num);

	g_debug("start writing flash, packets=%u, total_size=%u", num, data_size);

	for (idx = 0; idx < num; idx++) {
		guint32 packet_index;
		g_autoptr(FuChunk) chk = NULL;
		gsize count;
		const guint8 *chunk_data;
		guint retry_interval;
		FuPxiTpTfWritePacketCtx ctx;

		packet_index = idx + 1;

		chk = fu_chunk_array_index(chunks, (gsize)idx, error);
		if (chk == NULL)
			return FALSE;

		count = fu_chunk_get_data_sz(chk);
		chunk_data = fu_chunk_get_data(chk);

		ctx.packet_total = num;
		ctx.packet_index = packet_index;
		ctx.chunk_data = chunk_data;
		ctx.chunk_len = count;

		retry_interval = (send_interval > 0)
				     ? send_interval
				     : (guint)FU_PXI_TF_TIMING_DEFAULT_SEND_INTERVAL;

		if (!fu_device_retry_full(FU_DEVICE(self),
					  fu_pxi_tp_tf_communication_write_packet_cb,
					  (guint)FU_PXI_TF_RETRY_TIMES,
					  retry_interval,
					  &ctx,
					  error)) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "failed to write flash packet %u after %u retries",
				    packet_index,
				    (guint)FU_PXI_TF_RETRY_TIMES);
			fu_progress_reset(progress);
			return FALSE;
		}

		if (send_interval > 0)
			fu_device_sleep(FU_DEVICE(self), send_interval);

		fu_progress_step_done(progress);
	}

	g_debug("all packets sent, checking download status");

	{
		guint8 status = 0;
		guint16 mcu_packet_number = 0;

		if (!fu_pxi_tp_tf_communication_read_download_status(self,
								     &status,
								     &mcu_packet_number,
								     error)) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_WRITE,
					    "failed to read download status");
			return FALSE;
		}

		g_debug("download status: expected_packets=%u, device_packets=%u, status=%u",
			num,
			mcu_packet_number,
			status);

		if (status != 0 || mcu_packet_number != (guint16)num) {
			g_set_error(
			    error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "upgrade failed, status=%u, device_packets=%u, expected_packets=%u",
			    status,
			    mcu_packet_number,
			    num);
			return FALSE;
		}
	}

	fu_device_sleep(FU_DEVICE(self), (guint)FU_PXI_TF_TIMING_DOWNLOAD_POST_WAIT);

	g_debug("download status indicates success, exiting upgrade mode");
	if (!fu_pxi_tp_tf_communication_exit_upgrade_mode(self, NULL))
		g_debug("failed to exit upgrade mode (ignored)");

	fu_device_sleep(FU_DEVICE(self), (guint)FU_PXI_TF_TIMING_APP_VERSION_WAIT);
	return TRUE;
}

gboolean
fu_pxi_tp_tf_communication_write_firmware_process(FuPxiTpDevice *self,
						  FuProgress *progress,
						  guint32 send_interval,
						  guint32 data_size,
						  GByteArray *data,
						  GError **error)
{
	gsize i;
	g_autoptr(GError) ver_err = NULL;
	guint8 ver_before[3] = {0};

	g_debug("stop touchpad report");
	if (!fu_pxi_tp_register_user_write(self,
					   FU_PXI_TP_USER_BANK_BANK0,
					   FU_PXI_TP_REG_USER0_PROXY_MODE,
					   FU_PXI_TP_PROXY_MODE_TF_UPDATE,
					   error))
		return FALSE;

	if (!fu_pxi_tp_tf_communication_exit_upgrade_mode(self, NULL))
		g_debug("failed to exit upgrade mode (ignored)");

	if (fu_pxi_tp_tf_communication_read_firmware_version(self,
							     FU_PXI_TF_FW_MODE_APP,
							     ver_before,
							     &ver_err)) {
		g_debug("firmware version before update (mode=APP): %u.%u.%u",
			ver_before[0],
			ver_before[1],
			ver_before[2]);
	} else {
		g_debug("failed to read firmware version before update: %s",
			ver_err != NULL ? ver_err->message : "unknown error");
	}

	g_debug("validate rom header (bytes [%u, %u) must be 0x%02x)",
		(guint)FU_PXI_TF_LIMIT_ROM_HEADER_SKIP,
		(guint)FU_PXI_TF_LIMIT_ROM_HEADER_CHECK_END,
		(guint)FU_PXI_TF_LIMIT_ROM_HEADER_ZERO);

	if (data->len < (gsize)FU_PXI_TF_LIMIT_ROM_HEADER_CHECK_END) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "invalid firmware file: size too small for header check");
		return FALSE;
	}

	for (i = (gsize)FU_PXI_TF_LIMIT_ROM_HEADER_SKIP;
	     i < (gsize)FU_PXI_TF_LIMIT_ROM_HEADER_CHECK_END;
	     i++) {
		if (data->data[i] != (guint8)FU_PXI_TF_LIMIT_ROM_HEADER_ZERO) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_FILE,
					    "invalid rom file, non-zero data in header region");
			return FALSE;
		}
	}

	return fu_pxi_tp_tf_communication_write_firmware(self,
							 progress,
							 send_interval,
							 data_size,
							 data,
							 error);
}
