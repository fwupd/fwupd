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

/* ---- basic TF constants ---- */
#define FU_PXI_TF_FEATURE_REPORT_BYTE_LENGTH   64
#define FU_PXI_TF_WRITE_SIMPLE_CMD_REPORT_ID   0xCC
#define FU_PXI_TF_WRITE_SIMPLE_CMD_TARGET_ADDR 0x2C

#define FU_PXI_TF_FAILED_RETRY_TIMES	3
#define FU_PXI_TF_FAILED_RETRY_INTERVAL 10 /* ms */

/* ---- tf RMI frame layout ---- */
/* note: index 0 is REPORT_ID_PASS_THROUGH (0xCC) */
#define FU_PXI_TF_HDR_OFFSET_PREAMBLE	 1
#define FU_PXI_TF_HDR_OFFSET_TARGET_ADDR 2
#define FU_PXI_TF_HDR_OFFSET_FUNC_CODE	 3
#define FU_PXI_TF_HDR_OFFSET_DLEN0	 4
#define FU_PXI_TF_HDR_OFFSET_DLEN1	 5
#define FU_PXI_TF_HDR_HEADER_BYTES	 8 /* header up to len + replylen */

#define FU_PXI_TF_PAYLOAD_OFFSET_APP	 6 /* first app payload byte */
#define FU_PXI_TF_TAIL_CRC_OFFSET_BIAS	 6 /* CRC index = datalen + 6 */
#define FU_PXI_TF_TAIL_MAGIC_BYTE_OFFSET 7 /* tail index = datalen + 7 */

#define FU_PXI_TF_VERSION_BYTES		3
#define FU_PXI_TF_DOWNLOAD_STATUS_BYTES 3 /* status(1) + packet_number(2) */

/* ---- tf timing constants ---- */
#define FU_PXI_TF_RMI_REPLY_WAIT_MS	   10	/* wait for RMI reply */
#define FU_PXI_TF_BOOTLOADER_ENTER_WAIT_MS 100	/* after enter bootloader */
#define FU_PXI_TF_ERASE_WAIT_MS		   2000 /* erase flash wait time */
#define FU_PXI_TF_DOWNLOAD_POST_WAIT_MS	   50	/* after download status OK */
#define FU_PXI_TF_APP_VERSION_WAIT_MS	   1000 /* before/after app version read */
#define FU_PXI_TF_DEFAULT_SEND_INTERVAL_MS 50	/* fallback when send_interval==0 */
#define FU_PXI_TF_MAX_PACKET_DATA_LEN	   32	/* bytes per upgrade packet */

/* ---- rom header check spec ---- */
#define FU_PXI_TF_ROM_HEADER_SKIP_BYTES 6   /* bytes reserved for TF header */
#define FU_PXI_TF_ROM_HEADER_CHECK_END	128 /* check [6, 128) */
#define FU_PXI_TF_ROM_HEADER_ZERO	0x00

/* ---- tf update flow retry ---- */
#define FU_PXI_TF_UPDATE_FLOW_MAX_ATTEMPTS 3

/* ---- retry helper contexts ---- */

typedef struct {
	guint8 mode;
	guint8 *version;
} FuPxiTpTfReadFwVersionCtx;

typedef struct {
	guint8 *status;
	guint16 *packet_number;
} FuPxiTpTfReadDlStatusCtx;

/* --- tf Standard Communication helpers --- */

static gboolean
fu_pxi_tp_tf_communication_write_rmi_cmd(FuPxiTpDevice *self,
					 guint16 addr,
					 const guint8 *in_buf,
					 gsize in_bufsz,
					 GError **error)
{
	g_autoptr(FuStructPxiTfWriteSimpleCmd) st_write_simple = NULL;
	guint8 crc;
	guint8 tail;
	gsize need;

	/* build header using rustgen struct (endian-safe) */
	st_write_simple = fu_struct_pxi_tf_write_simple_cmd_new();
	if (st_write_simple == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "failed to allocate TF write simple header");
		return FALSE;
	}

	/* defaults are already set in the struct:
	 *   report_id  = 0xCC
	 *   preamble   = 0x5A
	 *   target_addr = 0x2C
	 *   func       = 0x00 (TF_FUNC_WRITE_SIMPLE)
	 */
	fu_struct_pxi_tf_write_simple_cmd_set_addr(st_write_simple, addr);
	fu_struct_pxi_tf_write_simple_cmd_set_len(st_write_simple, (guint16)in_bufsz);

	/* append payload directly into existing byte array */
	if (in_bufsz > 0 && in_buf != NULL)
		g_byte_array_append(st_write_simple->buf, in_buf, (guint)in_bufsz);

	/* CRC + tail:
	 * - CRC is computed from index 2 (preamble) to end of payload+header
	 * - tail is TF frame magic byte (0xA5)
	 */
	crc = fu_crc8(FU_CRC_KIND_B8_STANDARD,
		      st_write_simple->buf->data + 2,
		      st_write_simple->buf->len - 2);
	g_byte_array_append(st_write_simple->buf, &crc, 1);

	tail = FU_PXI_TF_FRAME_CONST_TAIL;
	g_byte_array_append(st_write_simple->buf, &tail, 1);

	need = FU_PXI_TF_FEATURE_REPORT_BYTE_LENGTH - st_write_simple->buf->len;
	if (need > 0) {
		guint8 zeros[64] = {0}; /* safe, max 64 */
		g_byte_array_append(st_write_simple->buf, zeros, need);
	}

	return fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(self),
					    st_write_simple->buf->data,
					    st_write_simple->buf->len,
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
	gsize datalen;
	g_autoptr(FuStructPxiTfWritePacketCmd) st_write_packet = NULL;
	guint8 crc;
	guint8 tail;
	gsize need;

	datalen = in_bufsz + 4; /* 2 bytes total + 2 bytes index */

	/* build header using rustgen struct (endian-safe) */
	st_write_packet = fu_struct_pxi_tf_write_packet_cmd_new();
	if (st_write_packet == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "failed to allocate TF write packet header");
		return FALSE;
	}

	/* defaults:
	 *   report_id  = 0xCC
	 *   preamble   = 0x5A
	 *   target_addr = 0x2C
	 *   func       = 0x04 (TF_FUNC_WRITE_WITH_PACK)
	 */
	fu_struct_pxi_tf_write_packet_cmd_set_addr(st_write_packet, addr);
	fu_struct_pxi_tf_write_packet_cmd_set_datalen(st_write_packet, (guint16)datalen);
	fu_struct_pxi_tf_write_packet_cmd_set_packet_total(st_write_packet, (guint16)packet_total);
	fu_struct_pxi_tf_write_packet_cmd_set_packet_index(st_write_packet, (guint16)packet_index);

	/* append payload directly into existing byte array */
	if (in_bufsz > 0 && in_buf != NULL)
		g_byte_array_append(st_write_packet->buf, in_buf, (guint)in_bufsz);

	/* CRC + tail */
	crc = fu_crc8(FU_CRC_KIND_B8_STANDARD,
		      st_write_packet->buf->data + 2,
		      st_write_packet->buf->len - 2);
	g_byte_array_append(st_write_packet->buf, &crc, 1);

	tail = FU_PXI_TF_FRAME_CONST_TAIL;
	g_byte_array_append(st_write_packet->buf, &tail, 1);

	need = FU_PXI_TF_FEATURE_REPORT_BYTE_LENGTH - st_write_packet->buf->len;
	if (need > 0) {
		guint8 zeros[64] = {0}; /* safe, max 64 */
		g_byte_array_append(st_write_packet->buf, zeros, need);
	}

	return fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(self),
					    st_write_packet->buf->data,
					    st_write_packet->buf->len,
					    FU_IOCTL_FLAG_NONE,
					    error);
}

static gboolean
fu_pxi_tp_tf_communication_read_rmi(FuPxiTpDevice *self,
				    guint16 addr,
				    const guint8 *in_buf,
				    gsize in_bufsz,
				    guint8 *out_buf,
				    gsize out_bufsz,
				    gsize *n_bytes_returned,
				    GError **error)
{
	gsize offset = 0;
	gsize datalen;
	g_autoptr(FuStructPxiTfReadCmd) st_read = NULL;
	guint8 crc;

	g_return_val_if_fail(out_buf != NULL, FALSE);
	g_return_val_if_fail(out_bufsz >= FU_PXI_TF_FEATURE_REPORT_BYTE_LENGTH, FALSE);

	memset(out_buf, 0, out_bufsz);

	/* build header using rustgen struct (endian-safe) */
	st_read = fu_struct_pxi_tf_read_cmd_new();
	if (st_read == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "failed to allocate TF read header");
		return FALSE;
	}

	/* nDataLen = input length + 2 bytes for reply length (low/high) */
	datalen = in_bufsz + 2;

	/* defaults:
	 *   report_id  = 0xCC
	 *   preamble   = 0x5A
	 *   target_addr = 0x2C
	 *   func       = 0x0B (TF_FUNC_READ_WITH_LEN)
	 */
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

	/* copy header into out_buf */
	if (!fu_memcpy_safe(out_buf,
			    out_bufsz,
			    0, /* dst_offset */
			    st_read->buf->data,
			    st_read->buf->len, /* src_sz */
			    0,		       /* src_offset */
			    st_read->buf->len, /* n */
			    error))
		return FALSE;

	offset = FU_STRUCT_PXI_TF_READ_CMD_SIZE;

	/* copy extra input payload (if any) */
	if (in_bufsz > 0 && in_buf != NULL) {
		if (!fu_memcpy_safe(out_buf,
				    out_bufsz,
				    offset,
				    in_buf,
				    in_bufsz, /* src_sz */
				    0,	      /* src_offset */
				    in_bufsz, /* n */
				    error))
			return FALSE;
		offset += in_bufsz;
	}

	/* crc + tail */
	crc = fu_crc8(FU_CRC_KIND_B8_STANDARD, out_buf + 2, offset - 2);
	out_buf[offset++] = crc;
	out_buf[offset++] = FU_PXI_TF_FRAME_CONST_TAIL;

	if (!fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(self),
					  out_buf,
					  FU_PXI_TF_FEATURE_REPORT_BYTE_LENGTH,
					  FU_IOCTL_FLAG_NONE,
					  error))
		return FALSE;

	fu_device_sleep(FU_DEVICE(self), FU_PXI_TF_RMI_REPLY_WAIT_MS);

	if (!fu_hidraw_device_get_feature(FU_HIDRAW_DEVICE(self),
					  out_buf,
					  FU_PXI_TF_FEATURE_REPORT_BYTE_LENGTH,
					  FU_IOCTL_FLAG_NONE,
					  error))
		return FALSE;

	/* parse reply header */
	if (out_buf[FU_PXI_TF_HDR_OFFSET_PREAMBLE] != FU_PXI_TF_FRAME_CONST_PREAMBLE ||
	    out_buf[FU_PXI_TF_HDR_OFFSET_TARGET_ADDR] != FU_PXI_TF_WRITE_SIMPLE_CMD_TARGET_ADDR) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "TF RMI read: invalid header 0x%02x 0x%02x",
			    out_buf[FU_PXI_TF_HDR_OFFSET_PREAMBLE],
			    out_buf[FU_PXI_TF_HDR_OFFSET_TARGET_ADDR]);
		return FALSE;
	}

	/* exception frame? */
	if ((out_buf[FU_PXI_TF_HDR_OFFSET_FUNC_CODE] & FU_PXI_TF_FRAME_CONST_EXCEPTION_FLAG) != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "TF RMI read: device returned exception 0x%02x",
			    out_buf[FU_PXI_TF_HDR_OFFSET_FUNC_CODE]);
		return FALSE;
	}

	/* datalen is payload length reported by device */
	datalen =
	    out_buf[FU_PXI_TF_HDR_OFFSET_DLEN0] + ((gsize)out_buf[FU_PXI_TF_HDR_OFFSET_DLEN1] << 8);
	if (fu_crc8(FU_CRC_KIND_B8_STANDARD, out_buf + 2, datalen + 4) !=
		out_buf[datalen + FU_PXI_TF_TAIL_CRC_OFFSET_BIAS] ||
	    out_buf[datalen + FU_PXI_TF_TAIL_MAGIC_BYTE_OFFSET] != FU_PXI_TF_FRAME_CONST_TAIL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "TF RMI read: CRC or tail mismatch");
		return FALSE;
	}

	if (n_bytes_returned != NULL)
		*n_bytes_returned = datalen + FU_PXI_TF_HDR_HEADER_BYTES;

	return TRUE;
}

/* --- fu_device_retry() callbacks --- */

static gboolean
fu_pxi_tp_tf_communication_read_firmware_version_cb(FuDevice *device,
						    gpointer user_data,
						    GError **error)
{
	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(device);
	FuPxiTpTfReadFwVersionCtx *ctx = user_data;
	guint8 in_buf[FU_PXI_TF_FEATURE_REPORT_BYTE_LENGTH] = {0};
	gsize len = FU_PXI_TF_VERSION_BYTES; /* expected payload length = 3 bytes (version) */

	if (!fu_pxi_tp_tf_communication_read_rmi(self,
						 FU_PXI_TF_CMD_READ_VERSION,
						 &ctx->mode,
						 1,
						 in_buf,
						 sizeof(in_buf),
						 &len,
						 error)) {
		return FALSE;
	}

	/* version bytes are at offset 6: [major, minor, patch] */
	if (!fu_memcpy_safe(ctx->version,
			    FU_PXI_TF_VERSION_BYTES, /* dst_sz */
			    0,			     /* dst_offset */
			    in_buf,
			    sizeof(in_buf),		  /* src_sz */
			    FU_PXI_TF_PAYLOAD_OFFSET_APP, /* src_offset */
			    FU_PXI_TF_VERSION_BYTES,	  /* n */
			    error)) {
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_pxi_tp_tf_communication_read_download_status_cb(FuDevice *device,
						   gpointer user_data,
						   GError **error)
{
	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(device);
	FuPxiTpTfReadDlStatusCtx *ctx = user_data;
	guint8 in_buf[FU_PXI_TF_FEATURE_REPORT_BYTE_LENGTH] = {0};
	gsize len = FU_PXI_TF_DOWNLOAD_STATUS_BYTES; /* payload len: status(1)+packet_number(2) */

	if (!fu_pxi_tp_tf_communication_read_rmi(self,
						 FU_PXI_TF_CMD_READ_UPGRADE_STATUS,
						 NULL,
						 0,
						 in_buf,
						 sizeof(in_buf),
						 &len,
						 error)) {
		return FALSE;
	}

	/* total frame length should be header(8) + payload(3) = 11 */
	if (len < FU_PXI_TF_HDR_HEADER_BYTES + FU_PXI_TF_DOWNLOAD_STATUS_BYTES ||
	    len - FU_PXI_TF_HDR_HEADER_BYTES != FU_PXI_TF_DOWNLOAD_STATUS_BYTES) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "download status reply has unexpected length: %" G_GSIZE_FORMAT,
			    len);
		return FALSE;
	}

	*(ctx->status) = in_buf[FU_PXI_TF_PAYLOAD_OFFSET_APP];
	*(ctx->packet_number) = (guint16)in_buf[FU_PXI_TF_PAYLOAD_OFFSET_APP + 1] +
				((guint16)in_buf[FU_PXI_TF_PAYLOAD_OFFSET_APP + 2] << 8);

	return TRUE;
}

/* mode: 1=APP, 2=BOOT, 3=ALGO (according to original protocol) */
static gboolean
fu_pxi_tp_tf_communication_read_firmware_version(FuPxiTpDevice *self,
						 guint8 mode,
						 guint8 *version,
						 GError **error)
{
	FuPxiTpTfReadFwVersionCtx ctx = {
	    .mode = mode,
	    .version = version,
	};

	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_pxi_tp_tf_communication_read_firmware_version_cb,
				  FU_PXI_TF_FAILED_RETRY_TIMES,
				  FU_PXI_TF_FAILED_RETRY_INTERVAL,
				  &ctx,
				  error)) {
		if (error != NULL && *error != NULL)
			g_prefix_error_literal(error, "failed to read firmware version: ");
		return FALSE;
	}

	return TRUE;
}

/* read TF upgrade download status (status + number of packets accepted by MCU) */
static gboolean
fu_pxi_tp_tf_communication_read_download_status(FuPxiTpDevice *self,
						guint8 *status,
						guint16 *packet_number,
						GError **error)
{
	FuPxiTpTfReadDlStatusCtx ctx = {
	    .status = status,
	    .packet_number = packet_number,
	};

	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_pxi_tp_tf_communication_read_download_status_cb,
				  FU_PXI_TF_FAILED_RETRY_TIMES,
				  FU_PXI_TF_FAILED_RETRY_INTERVAL,
				  &ctx,
				  error)) {
		if (error != NULL && *error != NULL)
			g_prefix_error_literal(error, "failed to read download status: ");
		return FALSE;
	}

	return TRUE;
}

/* perform one TF firmware update attempt: no outer retries here */
static gboolean
fu_pxi_tp_tf_communication_write_firmware(FuPxiTpDevice *self,
					  FuProgress *progress,
					  guint32 send_interval,
					  guint32 data_size,
					  const GByteArray *data,
					  GError **error)
{
	const guint32 max_packet_data_len = FU_PXI_TF_MAX_PACKET_DATA_LEN;
	guint8 touch_operate_buf = FU_PXI_TF_TOUCH_CONTROL_DISABLE;
	guint8 tmp_buf[FU_PXI_TF_FEATURE_REPORT_BYTE_LENGTH] = {0};
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(FuChunkArray) chunks = NULL;
	gsize num_chunks = 0;
	guint32 num = 0;
	guint32 idx;

	(void)progress; /* currently unused, can be wired to progress if needed */

	/* disable touch function while updating TF */
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

	/* enter TF bootloader / upgrade mode */
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

	fu_device_sleep(FU_DEVICE(self), FU_PXI_TF_BOOTLOADER_ENTER_WAIT_MS);

	/* erase flash before programming */
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

	fu_device_sleep(FU_DEVICE(self), FU_PXI_TF_ERASE_WAIT_MS);

	/* build chunk array from firmware payload */
	blob = g_bytes_new(data->data, data_size);
	chunks = fu_chunk_array_new_from_bytes(blob,
					       FU_CHUNK_ADDR_OFFSET_NONE,
					       FU_CHUNK_PAGESZ_NONE,
					       max_packet_data_len);
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

	/* total number of packets */
	num = (guint32)num_chunks;

	g_debug("start writing flash, packets=%u, total_size=%u", num, data_size);

	for (idx = 0; idx < num; idx++) {
		guint32 packet_index = idx + 1;
		g_autoptr(FuChunk) chk = fu_chunk_array_index(chunks, idx, error);
		gsize count;
		const guint8 *chunk_data;
		guint32 k;

		if (chk == NULL)
			return FALSE;

		count = fu_chunk_get_data_sz(chk);
		chunk_data = fu_chunk_get_data(chk);
		k = 0;

		for (; k < FU_PXI_TF_FAILED_RETRY_TIMES; k++) {
			g_autoptr(GError) error_local = NULL;

			if (fu_pxi_tp_tf_communication_write_rmi_with_packet(
				self,
				FU_PXI_TF_CMD_WRITE_UPGRADE_DATA,
				num,
				packet_index,
				chunk_data,
				count,
				&error_local)) {
				break; /* this packet succeeded */
			}

			g_debug("packet %u write failed, attempt %u/%d: %s",
				packet_index,
				k + 1,
				FU_PXI_TF_FAILED_RETRY_TIMES,
				error_local != NULL ? error_local->message : "unknown");

			if (k < FU_PXI_TF_FAILED_RETRY_TIMES - 1)
				fu_device_sleep(FU_DEVICE(self),
						send_interval > 0
						    ? send_interval
						    : FU_PXI_TF_DEFAULT_SEND_INTERVAL_MS);
		}

		if (k == FU_PXI_TF_FAILED_RETRY_TIMES) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "failed to write flash packet %u after %d retries",
				    packet_index,
				    FU_PXI_TF_FAILED_RETRY_TIMES);
			return FALSE;
		}

		/* small delay between packets */
		if (send_interval > 0)
			fu_device_sleep(FU_DEVICE(self), send_interval);
	}

	g_debug("all packets sent, checking download status");

	/* read back download status from device */
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

		g_debug("download status OK, expected_packets=%u, device_packets=%u, status=%u",
			num,
			mcu_packet_number,
			status);

		if (status != 0 || mcu_packet_number != num) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "upgrade failed, status=%u, device_packets=%u, "
				    "expected_packets=%u",
				    status,
				    mcu_packet_number,
				    num);
			return FALSE;
		}
	}

	fu_device_sleep(FU_DEVICE(self), FU_PXI_TF_DOWNLOAD_POST_WAIT_MS);
	g_debug("download status indicates success, exiting upgrade mode");

	/* exit upgrade mode (best-effort) to get the fw version not bootloader version */
	if (!fu_pxi_tp_tf_communication_exit_upgrade_mode(self, NULL))
		g_debug("failed to exit upgrade mode (ignored)");

	return TRUE;
}

/* compare TF firmware version: returns <0 if a<b, 0 if a==b, >0 if a>b */
static gint
fu_pxi_tp_tf_communication_version_cmp(const guint8 a[3], const guint8 b[3])
{
	gsize i;

	for (i = 0; i < 3; i++) {
		if (a[i] < b[i])
			return -1;
		if (a[i] > b[i])
			return 1;
	}
	return 0;
}

/* Public entry point used by the main plugin:
 * - reads TF version before update (mode=APP)
 * - skips update if current version >= target version
 * - validates TF image
 * - stops touchpad report
 * - retries the TF update a few times at high level
 * - reads TF version after successful update
 * - verifies TF version matches target version
 */
gboolean
fu_pxi_tp_tf_communication_write_firmware_process(FuPxiTpDevice *self,
						  FuProgress *progress,
						  guint32 send_interval,
						  guint32 data_size,
						  GByteArray *data,
						  const guint8 target_ver[3],
						  GError **error)
{
	guint8 ver_before[3] = {0};
	g_autoptr(GError) ver_err = NULL;
	gsize i;
	gsize attempt;

	fu_device_sleep(FU_DEVICE(self), FU_PXI_TF_APP_VERSION_WAIT_MS);

	/* stop touchpad reports while updating TF */
	g_debug("stop touchpad report");
	if (!fu_pxi_tp_register_user_write(self,
					   FU_PXI_TP_USER_BANK_BANK0,
					   FU_PXI_TP_REG_USER0_PROXY_MODE,
					   FU_PXI_TP_PROXY_MODE_TF_UPDATE,
					   error))
		return FALSE;

	/* exit upgrade mode (best-effort) to get the fw version not bootloader version */
	if (!fu_pxi_tp_tf_communication_exit_upgrade_mode(self, NULL))
		g_debug("failed to exit upgrade mode (ignored)");

	/* try to read TF firmware version before update (mode=APP) */
	if (fu_pxi_tp_tf_communication_read_firmware_version(self,
							     FU_PXI_TF_FW_MODE_APP,
							     ver_before,
							     &ver_err)) {
		g_debug("firmware version before update (mode=APP): %u.%u.%u",
			ver_before[0],
			ver_before[1],
			ver_before[2]);

		/* if current version >= target, skip update */
		if (target_ver != NULL &&
		    fu_pxi_tp_tf_communication_version_cmp(ver_before, target_ver) >= 0) {
			g_debug("current FW %u.%u.%u >= target %u.%u.%u, skip update",
				ver_before[0],
				ver_before[1],
				ver_before[2],
				target_ver[0],
				target_ver[1],
				target_ver[2]);
			return TRUE;
		}
	} else {
		g_debug("failed to read firmware version before update: %s",
			ver_err != NULL ? ver_err->message : "unknown error");
	}

	/* sanity check: bytes [6, 128) must be zero (ROM header rule) */
	g_debug("validate ROM header (bytes [%d, %d) must be 0x%02x)",
		FU_PXI_TF_ROM_HEADER_SKIP_BYTES,
		FU_PXI_TF_ROM_HEADER_CHECK_END,
		(guint)FU_PXI_TF_ROM_HEADER_ZERO);

	for (i = FU_PXI_TF_ROM_HEADER_SKIP_BYTES; i < FU_PXI_TF_ROM_HEADER_CHECK_END; i++) {
		if (data->data[i] != FU_PXI_TF_ROM_HEADER_ZERO) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_FILE,
					    "invalid ROM file, non-zero data in header region");
			return FALSE;
		}
	}

	for (attempt = 1; attempt <= FU_PXI_TF_UPDATE_FLOW_MAX_ATTEMPTS; attempt++) {
		g_autoptr(GError) error_local = NULL;

		g_debug("firmware update attempt %" G_GSIZE_FORMAT "/%d",
			attempt,
			FU_PXI_TF_UPDATE_FLOW_MAX_ATTEMPTS);

		if (fu_pxi_tp_tf_communication_write_firmware(self,
							      progress,
							      send_interval,
							      data_size,
							      data,
							      &error_local)) {
			/* read tf firmware version after successful update */
			fu_device_sleep(FU_DEVICE(self), FU_PXI_TF_APP_VERSION_WAIT_MS);
			{
				guint8 ver_after[3] = {0};
				g_autoptr(GError) ver_after_err = NULL;

				if (!fu_pxi_tp_tf_communication_read_firmware_version(
					self,
					FU_PXI_TF_FW_MODE_APP,
					ver_after,
					&ver_after_err)) {
					g_set_error(
					    error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_READ,
					    "failed to read firmware version after update: %s",
					    ver_after_err != NULL ? ver_after_err->message
								  : "unknown error");
					return FALSE;
				}

				g_debug("firmware version after update (mode=APP): %u.%u.%u",
					ver_after[0],
					ver_after[1],
					ver_after[2]);

				/* verify version matches target version */
				if (target_ver != NULL &&
				    fu_pxi_tp_tf_communication_version_cmp(ver_after, target_ver) !=
					0) {
					g_set_error(error,
						    FWUPD_ERROR,
						    FWUPD_ERROR_INVALID_FILE,
						    "firmware version after update (%u.%u.%u) "
						    "does not match target (%u.%u.%u)",
						    ver_after[0],
						    ver_after[1],
						    ver_after[2],
						    target_ver[0],
						    target_ver[1],
						    target_ver[2]);
					return FALSE;
				}
			}

			g_debug("firmware update succeeded on attempt %" G_GSIZE_FORMAT, attempt);
			return TRUE;
		}

		g_debug("firmware update attempt %" G_GSIZE_FORMAT " failed: %s",
			attempt,
			error_local != NULL ? error_local->message : "unknown error");

		if (attempt < FU_PXI_TF_UPDATE_FLOW_MAX_ATTEMPTS)
			g_debug("retrying firmware update");
	}

	/* all attempts failed, report a single error to caller */
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_WRITE,
		    "firmware update failed after %" G_GSIZE_FORMAT " attempts",
		    (gsize)FU_PXI_TF_UPDATE_FLOW_MAX_ATTEMPTS);
	return FALSE;
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
