/*
 * Copyright 2025 Harris Tai <harris_tai@pixart.com>
 * Copyright 2025 Micky Hsieh <micky_hsieh@pixart.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-pxi-tp-register.h"
#include "fu-pxi-tp-struct.h" /* for FuStructPxiTf*Cmd */
#include "fu-pxi-tp-tf-communication.h"

#define REPORT_ID__PASS_THROUGH 0xCC
#define SLAVE_ADDRESS		0x2C

#define FAILED_RETRY_TIMES    3
#define FAILED_RETRY_INTERVAL 10 /* ms */

#define SET_UPGRADE_MODE    0x0001
#define WRITE_UPGRADE_DATA  0x0002
#define READ_UPGRADE_STATUS 0x0003
#define READ_VERSION_CMD    0x0007
#define TOUCH_CONTROL	    0x0303

#define TF_FEATURE_REPORT_BYTE_LENGTH 64

/* ---- TF RMI framing / function codes ---- */
enum {
	TF_FRAME_PREAMBLE = 0x5A,
	TF_FRAME_TAIL = 0xA5,

	TF_FUNC_WRITE_SIMPLE = 0x00,
	TF_FUNC_WRITE_WITH_PACK = 0x04,
	TF_FUNC_READ_WITH_LEN = 0x0B,

	TF_EXCEPTION_FLAG = 0x80,
};

/* ---- TF RMI frame layout ---- */
enum {
	/* note: index 0 is REPORT_ID__PASS_THROUGH */
	TF_HDR_OFFSET_PREAMBLE = 1,
	TF_HDR_OFFSET_SLAVE_ADDR = 2,
	TF_HDR_OFFSET_FUNC_CODE = 3,
	TF_HDR_OFFSET_DLEN0 = 4,
	TF_HDR_OFFSET_DLEN1 = 5,
	TF_HDR_HEADER_BYTES = 8, /* header up to len + replylen */

	TF_PAYLOAD_OFFSET_APP = 6,     /* first app payload byte */
	TF_TAIL_CRC_OFFSET_BIAS = 6,   /* CRC index = datalen + 6 */
	TF_TAIL_MAGIC_BYTE_OFFSET = 7, /* tail index = datalen + 7 */

	TF_VERSION_BYTES = 3,
	TF_DOWNLOAD_STATUS_BYTES = 3, /* status(1) + packet_number(2) */
};

/* ---- TF upgrade mode payload ---- */
enum {
	TF_UPGRADE_MODE_EXIT = 0x00,
	TF_UPGRADE_MODE_ENTER_BOOT = 0x01,
	TF_UPGRADE_MODE_ERASE_FLASH = 0x02,
};

/* ---- TF touch control payload ---- */
enum {
	TF_TOUCH_ENABLE = 0x00,
	TF_TOUCH_DISABLE = 0x01,
};

/* ---- TF timing constants ---- */
enum {
	TF_RMI_REPLY_WAIT_MS = 10,	   /* wait for RMI reply */
	TF_BOOTLOADER_ENTER_WAIT_MS = 100, /* after enter bootloader */
	TF_ERASE_WAIT_MS = 2000,	   /* erase flash wait time */
	TF_DOWNLOAD_POST_WAIT_MS = 50,	   /* after download status OK */
	TF_APP_VERSION_WAIT_MS = 1000,	   /* before/after app version read */
	TF_DEFAULT_SEND_INTERVAL_MS = 50,  /* fallback when send_interval==0 */
	TF_MAX_PACKET_DATA_LEN = 32,	   /* bytes per upgrade packet */
};

/* ---- ROM header check spec ---- */
enum {
	TF_ROM_HEADER_SKIP_BYTES = 6,  /* bytes reserved for TF header */
	TF_ROM_HEADER_CHECK_END = 128, /* check [6, 128) */
	TF_ROM_HEADER_ZERO = 0x00,
};

/* ---- TF firmware version mode ---- */
enum {
	TF_FW_MODE_APP = 1,
	TF_FW_MODE_BOOT = 2,
	TF_FW_MODE_ALGO = 3,
};

/* ---- TF update flow retry ---- */
enum {
	TF_UPDATE_FLOW_MAX_ATTEMPTS = 3,
};

/* ---- TP proxy mode (host<->TF pass-through) ---- */
enum {
	PXI_TP_PROXY_MODE_NORMAL = 0x00,
	PXI_TP_PROXY_MODE_TF_UPDATE = 0x01,
};

/* --- tf Standard Communication helpers --- */

static gboolean
fu_pxi_tp_tf_communication_write_rmi_cmd(FuPxiTpDevice *self,
					 guint16 addr,
					 const guint8 *in_buf,
					 gsize in_bufsz,
					 GError **error)
{
	guint8 buf[TF_FEATURE_REPORT_BYTE_LENGTH] = {0};
	gsize offset = 0;

	/* build header using rustgen struct (endian-safe) */
	g_autoptr(FuStructPxiTfWriteSimpleCmd) st_write_simple =
	    fu_struct_pxi_tf_write_simple_cmd_new();
	if (st_write_simple == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "failed to allocate TF write simple header");
		return FALSE;
	}

	fu_struct_pxi_tf_write_simple_cmd_set_report_id(st_write_simple, REPORT_ID__PASS_THROUGH);
	fu_struct_pxi_tf_write_simple_cmd_set_preamble(st_write_simple, TF_FRAME_PREAMBLE);
	fu_struct_pxi_tf_write_simple_cmd_set_slave_addr(st_write_simple, SLAVE_ADDRESS);
	fu_struct_pxi_tf_write_simple_cmd_set_func(st_write_simple, TF_FUNC_WRITE_SIMPLE);
	fu_struct_pxi_tf_write_simple_cmd_set_addr(st_write_simple, addr);
	fu_struct_pxi_tf_write_simple_cmd_set_len(st_write_simple, (guint16)in_bufsz);

	/* copy header into feature report buffer */
	if (!fu_memcpy_safe(buf,
			    sizeof(buf),
			    0, /* dst_offset */
			    st_write_simple->buf->data,
			    st_write_simple->buf->len, /* src_sz */
			    0,			       /* src_offset */
			    st_write_simple->buf->len, /* n */
			    error))
		return FALSE;

	offset = FU_STRUCT_PXI_TF_WRITE_SIMPLE_CMD_SIZE;

	/* copy payload */
	if (in_bufsz > 0 && in_buf != NULL) {
		if (!fu_memcpy_safe(buf,
				    sizeof(buf),
				    offset,
				    in_buf,
				    in_bufsz, /* src_sz */
				    0,	      /* src_offset */
				    in_bufsz, /* n */
				    error))
			return FALSE;
		offset += in_bufsz;
	}

	/* CRC + tail */
	buf[offset++] = fu_crc8(FU_CRC_KIND_B8_STANDARD, buf + 2, offset - 2);
	buf[offset++] = TF_FRAME_TAIL;

	return fu_pxi_tp_common_send_feature(self, buf, TF_FEATURE_REPORT_BYTE_LENGTH, error);
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
	guint8 buf[TF_FEATURE_REPORT_BYTE_LENGTH] = {0};
	gsize offset = 0;
	gsize datalen = in_bufsz + 4; /* 2 bytes total + 2 bytes index */

	/* build header using rustgen struct (endian-safe) */
	g_autoptr(FuStructPxiTfWritePacketCmd) st_write_packet =
	    fu_struct_pxi_tf_write_packet_cmd_new();
	if (st_write_packet == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "failed to allocate TF write packet header");
		return FALSE;
	}

	fu_struct_pxi_tf_write_packet_cmd_set_report_id(st_write_packet, REPORT_ID__PASS_THROUGH);
	fu_struct_pxi_tf_write_packet_cmd_set_preamble(st_write_packet, TF_FRAME_PREAMBLE);
	fu_struct_pxi_tf_write_packet_cmd_set_slave_addr(st_write_packet, SLAVE_ADDRESS);
	fu_struct_pxi_tf_write_packet_cmd_set_func(st_write_packet, TF_FUNC_WRITE_WITH_PACK);
	fu_struct_pxi_tf_write_packet_cmd_set_addr(st_write_packet, addr);
	fu_struct_pxi_tf_write_packet_cmd_set_datalen(st_write_packet, (guint16)datalen);
	fu_struct_pxi_tf_write_packet_cmd_set_packet_total(st_write_packet, (guint16)packet_total);
	fu_struct_pxi_tf_write_packet_cmd_set_packet_index(st_write_packet, (guint16)packet_index);

	/* copy header into feature report buffer */
	if (!fu_memcpy_safe(buf,
			    sizeof(buf),
			    0, /* dst_offset */
			    st_write_packet->buf->data,
			    st_write_packet->buf->len, /* src_sz */
			    0,			       /* src_offset */
			    st_write_packet->buf->len, /* n */
			    error))
		return FALSE;

	offset = FU_STRUCT_PXI_TF_WRITE_PACKET_CMD_SIZE;

	/* copy payload */
	if (in_bufsz > 0 && in_buf != NULL) {
		if (!fu_memcpy_safe(buf,
				    sizeof(buf),
				    offset,
				    in_buf,
				    in_bufsz, /* src_sz */
				    0,	      /* src_offset */
				    in_bufsz, /* n */
				    error))
			return FALSE;
		offset += in_bufsz;
	}

	/* CRC + tail */
	buf[offset++] = fu_crc8(FU_CRC_KIND_B8_STANDARD, buf + 2, offset - 2);
	buf[offset++] = TF_FRAME_TAIL;

	return fu_pxi_tp_common_send_feature(self, buf, TF_FEATURE_REPORT_BYTE_LENGTH, error);
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

	g_return_val_if_fail(out_buf != NULL, FALSE);
	g_return_val_if_fail(out_bufsz >= TF_FEATURE_REPORT_BYTE_LENGTH, FALSE);

	memset(out_buf, 0, out_bufsz);

	/* build header using rustgen struct (endian-safe) */
	g_autoptr(FuStructPxiTfReadCmd) st_read = fu_struct_pxi_tf_read_cmd_new();
	if (st_read == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "failed to allocate TF read header");
		return FALSE;
	}

	/* nDataLen = input length + 2 bytes for reply length (low/high) */
	datalen = in_bufsz + 2;

	fu_struct_pxi_tf_read_cmd_set_report_id(st_read, REPORT_ID__PASS_THROUGH);
	fu_struct_pxi_tf_read_cmd_set_preamble(st_read, TF_FRAME_PREAMBLE);
	fu_struct_pxi_tf_read_cmd_set_slave_addr(st_read, SLAVE_ADDRESS);
	fu_struct_pxi_tf_read_cmd_set_func(st_read, TF_FUNC_READ_WITH_LEN);
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

	/* CRC + tail */
	out_buf[offset++] = fu_crc8(FU_CRC_KIND_B8_STANDARD, out_buf + 2, offset - 2);
	out_buf[offset++] = TF_FRAME_TAIL;

	if (!fu_pxi_tp_common_send_feature(self, out_buf, TF_FEATURE_REPORT_BYTE_LENGTH, error))
		return FALSE;

	fu_device_sleep(FU_DEVICE(self), TF_RMI_REPLY_WAIT_MS);

	if (!fu_pxi_tp_common_get_feature(self,
					  REPORT_ID__PASS_THROUGH,
					  out_buf,
					  TF_FEATURE_REPORT_BYTE_LENGTH,
					  error))
		return FALSE;

	/* parse reply header */
	if (out_buf[TF_HDR_OFFSET_PREAMBLE] != TF_FRAME_PREAMBLE ||
	    out_buf[TF_HDR_OFFSET_SLAVE_ADDR] != SLAVE_ADDRESS) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "TF RMI read: invalid header 0x%02x 0x%02x",
			    out_buf[TF_HDR_OFFSET_PREAMBLE],
			    out_buf[TF_HDR_OFFSET_SLAVE_ADDR]);
		return FALSE;
	}

	/* exception frame? */
	if ((out_buf[TF_HDR_OFFSET_FUNC_CODE] & TF_EXCEPTION_FLAG) != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "TF RMI read: device returned exception 0x%02x",
			    out_buf[TF_HDR_OFFSET_FUNC_CODE]);
		return FALSE;
	}

	/* datalen is payload length reported by device */
	datalen = out_buf[TF_HDR_OFFSET_DLEN0] + ((gsize)out_buf[TF_HDR_OFFSET_DLEN1] << 8);
	if (fu_crc8(FU_CRC_KIND_B8_STANDARD, out_buf + 2, datalen + 4) !=
		out_buf[datalen + TF_TAIL_CRC_OFFSET_BIAS] ||
	    out_buf[datalen + TF_TAIL_MAGIC_BYTE_OFFSET] != TF_FRAME_TAIL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "TF RMI read: CRC or tail mismatch");
		return FALSE;
	}

	if (n_bytes_returned != NULL)
		*n_bytes_returned = datalen + TF_HDR_HEADER_BYTES;

	return TRUE;
}

/* mode: 1=APP, 2=BOOT, 3=ALGO (according to original protocol) */
static gboolean
fu_pxi_tp_tf_communication_read_firmware_version(FuPxiTpDevice *self,
						 guint8 mode,
						 guint8 *version,
						 GError **error)
{
	guint8 in_buf[TF_FEATURE_REPORT_BYTE_LENGTH] = {0};
	gsize len = TF_VERSION_BYTES; /* expected payload length = 3 bytes (version) */

	for (gsize i = 0; i < FAILED_RETRY_TIMES; i++) {
		g_autoptr(GError) local_error = NULL;

		if (fu_pxi_tp_tf_communication_read_rmi(self,
							READ_VERSION_CMD,
							&mode,
							1,
							in_buf,
							sizeof(in_buf),
							&len,
							&local_error)) {
			/* version bytes are at offset 6: [major, minor, patch] */
			if (!fu_memcpy_safe(version,
					    TF_VERSION_BYTES, /* dst_sz */
					    0,		      /* dst_offset */
					    in_buf,
					    sizeof(in_buf),	   /* src_sz */
					    TF_PAYLOAD_OFFSET_APP, /* src_offset */
					    TF_VERSION_BYTES,	   /* n */
					    error))
				return FALSE;

			return TRUE;
		}

		g_debug("read firmware version failed try %" G_GSIZE_FORMAT "/%d: %s",
			i + 1,
			FAILED_RETRY_TIMES,
			local_error != NULL ? local_error->message : "unknown");

		if (i < FAILED_RETRY_TIMES - 1)
			fu_device_sleep(FU_DEVICE(self), FAILED_RETRY_INTERVAL);
	}

	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_WRITE,
		    "failed to read firmware version after %d retries",
		    FAILED_RETRY_TIMES);
	return FALSE;
}

/* read TF upgrade download status (status + number of packets accepted by MCU) */
static gboolean
fu_pxi_tp_tf_communication_read_download_status(FuPxiTpDevice *self,
						guint8 *status,
						guint16 *packet_number,
						GError **error)
{
	guint8 in_buf[TF_FEATURE_REPORT_BYTE_LENGTH] = {0};
	gsize len = TF_DOWNLOAD_STATUS_BYTES; /* payload length: status(1) + packet_number(2) */

	for (gsize i = 0; i < FAILED_RETRY_TIMES; i++) {
		g_autoptr(GError) local_error = NULL;

		if (fu_pxi_tp_tf_communication_read_rmi(self,
							READ_UPGRADE_STATUS,
							NULL,
							0,
							in_buf,
							sizeof(in_buf),
							&len,
							&local_error)) {
			/* total frame length should be header(8) + payload(3) = 11 */
			if (len >= TF_HDR_HEADER_BYTES + TF_DOWNLOAD_STATUS_BYTES &&
			    len - TF_HDR_HEADER_BYTES == TF_DOWNLOAD_STATUS_BYTES) {
				*status = in_buf[TF_PAYLOAD_OFFSET_APP];
				*packet_number = (guint16)in_buf[TF_PAYLOAD_OFFSET_APP + 1] +
						 ((guint16)in_buf[TF_PAYLOAD_OFFSET_APP + 2] << 8);
				return TRUE;
			}

			g_debug("download status reply has unexpected length: %" G_GSIZE_FORMAT,
				len);
			/* treat as failure and retry */
		}

		g_debug("read download status failed try %" G_GSIZE_FORMAT "/%d: %s",
			i + 1,
			FAILED_RETRY_TIMES,
			local_error != NULL ? local_error->message : "unknown");

		if (i < FAILED_RETRY_TIMES - 1)
			fu_device_sleep(FU_DEVICE(self), FAILED_RETRY_INTERVAL);
	}

	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_WRITE,
		    "failed to read download status after %d retries",
		    FAILED_RETRY_TIMES);
	return FALSE;
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
	const guint32 max_packet_data_len = TF_MAX_PACKET_DATA_LEN;

	(void)progress; /* currently unused, can be wired to progress if needed */

	/* disable touch function while updating TF */
	g_debug("disabling touch");
	guint8 touch_operate_buf = TF_TOUCH_DISABLE;
	if (!fu_pxi_tp_tf_communication_write_rmi_cmd(self,
						      TOUCH_CONTROL,
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
	guint8 tmp_buf[TF_FEATURE_REPORT_BYTE_LENGTH] = {0};
	tmp_buf[0] = TF_UPGRADE_MODE_ENTER_BOOT;
	if (!fu_pxi_tp_tf_communication_write_rmi_cmd(self, SET_UPGRADE_MODE, tmp_buf, 1, error)) {
		/* best-effort rollback: re-enable touch, ignore errors */
		touch_operate_buf = TF_TOUCH_ENABLE;
		g_debug("re-enabling touch after bootloader enter failure");
		(void)fu_pxi_tp_tf_communication_write_rmi_cmd(self,
							       TOUCH_CONTROL,
							       &touch_operate_buf,
							       1,
							       NULL);

		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "failed to enter bootloader mode");
		return FALSE;
	}

	fu_device_sleep(FU_DEVICE(self), TF_BOOTLOADER_ENTER_WAIT_MS);

	/* erase flash before programming */
	g_debug("erase flash");
	tmp_buf[0] = TF_UPGRADE_MODE_ERASE_FLASH;
	if (!fu_pxi_tp_tf_communication_write_rmi_cmd(self, SET_UPGRADE_MODE, tmp_buf, 1, error)) {
		touch_operate_buf = TF_TOUCH_ENABLE;
		g_debug("re-enabling touch after erase command failure");
		(void)fu_pxi_tp_tf_communication_write_rmi_cmd(self,
							       TOUCH_CONTROL,
							       &touch_operate_buf,
							       1,
							       NULL);

		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "failed to send erase flash command");
		return FALSE;
	}

	fu_device_sleep(FU_DEVICE(self), TF_ERASE_WAIT_MS);

	/* calculate total number of packets */
	guint32 num = (data_size + max_packet_data_len - 1) / max_packet_data_len;
	guint32 offset = 0;

	g_debug("start writing flash, packets=%u, total_size=%u", num, data_size);

	for (guint32 i = 1; i <= num; i++, offset += max_packet_data_len) {
		gsize count = (i == num) ? (data_size - offset) : max_packet_data_len;
		guint32 k = 0;

		for (; k < FAILED_RETRY_TIMES; k++) {
			g_autoptr(GError) local_error = NULL;

			if (fu_pxi_tp_tf_communication_write_rmi_with_packet(self,
									     WRITE_UPGRADE_DATA,
									     num,
									     i,
									     &data->data[offset],
									     count,
									     &local_error)) {
				break; /* this packet succeeded */
			}

			g_debug("packet %u write failed, attempt %u/%d: %s",
				i,
				k + 1,
				FAILED_RETRY_TIMES,
				local_error != NULL ? local_error->message : "unknown");

			if (k < FAILED_RETRY_TIMES - 1)
				fu_device_sleep(FU_DEVICE(self),
						send_interval > 0 ? send_interval
								  : TF_DEFAULT_SEND_INTERVAL_MS);
		}

		if (k == FAILED_RETRY_TIMES) {
			/* give up on this packet: rollback touch and fail */
			touch_operate_buf = TF_TOUCH_ENABLE;
			g_debug("re-enabling touch after packet %u write failure", i);
			(void)fu_pxi_tp_tf_communication_write_rmi_cmd(self,
								       TOUCH_CONTROL,
								       &touch_operate_buf,
								       1,
								       NULL);

			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "failed to write flash packet %u after %d retries",
				    i,
				    FAILED_RETRY_TIMES);
			return FALSE;
		}

		/* small delay between packets */
		if (send_interval > 0)
			fu_device_sleep(FU_DEVICE(self), send_interval);
	}

	g_debug("all packets sent, checking download status");

	/* read back download status from device */
	guint8 status = 0;
	guint16 mcu_packet_number = 0;
	if (!fu_pxi_tp_tf_communication_read_download_status(self,
							     &status,
							     &mcu_packet_number,
							     error)) {
		touch_operate_buf = TF_TOUCH_ENABLE;
		g_debug("re-enabling touch after download status read failure");
		(void)fu_pxi_tp_tf_communication_write_rmi_cmd(self,
							       TOUCH_CONTROL,
							       &touch_operate_buf,
							       1,
							       NULL);

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
		touch_operate_buf = TF_TOUCH_ENABLE;
		g_debug("re-enabling touch after download status mismatch");
		(void)fu_pxi_tp_tf_communication_write_rmi_cmd(self,
							       TOUCH_CONTROL,
							       &touch_operate_buf,
							       1,
							       NULL);

		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "upgrade failed, status=%u, device_packets=%u, expected_packets=%u",
			    status,
			    mcu_packet_number,
			    num);
		return FALSE;
	}

	fu_device_sleep(FU_DEVICE(self), TF_DOWNLOAD_POST_WAIT_MS);
	g_debug("download status indicates success, exiting upgrade mode");

	/* exit upgrade mode (best-effort) */
	tmp_buf[0] = TF_UPGRADE_MODE_EXIT;
	if (!fu_pxi_tp_tf_communication_write_rmi_cmd(self, SET_UPGRADE_MODE, tmp_buf, 1, NULL))
		g_debug("failed to exit upgrade mode (ignored)");

	/* re-enable touch (best-effort) */
	touch_operate_buf = TF_TOUCH_ENABLE;
	g_debug("re-enabling touch after successful upgrade");
	if (!fu_pxi_tp_tf_communication_write_rmi_cmd(self,
						      TOUCH_CONTROL,
						      &touch_operate_buf,
						      1,
						      NULL))
		g_debug("failed to re-enable touch (ignored)");

	return TRUE;
}

/* compare TF firmware version: returns <0 if a<b, 0 if a==b, >0 if a>b */
static gint
fu_pxi_tp_tf_communication_version_cmp(const guint8 a[3], const guint8 b[3])
{
	for (gsize i = 0; i < 3; i++) {
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
	gboolean have_ver_before = FALSE;
	g_autoptr(GError) ver_err = NULL;

	fu_device_sleep(FU_DEVICE(self), TF_APP_VERSION_WAIT_MS);

	/* stop touchpad reports while updating TF */
	g_debug("stop touchpad report");
	if (!fu_pxi_tp_register_user_write(self,
					   PXI_TP_USER_BANK0,
					   PXI_TP_R_USER0_PROXY_MODE,
					   PXI_TP_PROXY_MODE_TF_UPDATE,
					   error))
		return FALSE;

	/* try to read TF firmware version before update (mode=APP) */
	if (fu_pxi_tp_tf_communication_read_firmware_version(self,
							     TF_FW_MODE_APP,
							     ver_before,
							     &ver_err)) {
		g_debug("firmware version before update (mode=APP): %u.%u.%u",
			ver_before[0],
			ver_before[1],
			ver_before[2]);
		have_ver_before = TRUE;

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

			/* best-effort restore proxy mode */
			(void)fu_pxi_tp_register_user_write(self,
							    PXI_TP_USER_BANK0,
							    PXI_TP_R_USER0_PROXY_MODE,
							    PXI_TP_PROXY_MODE_NORMAL,
							    NULL);
			return TRUE;
		}
	} else {
		g_debug("failed to read firmware version before update: %s",
			ver_err != NULL ? ver_err->message : "unknown error");
	}

	/* sanity check: bytes [6, 128) must be zero (ROM header rule) */
	g_debug("validate ROM header (bytes [%d, %d) must be 0x%02x)",
		TF_ROM_HEADER_SKIP_BYTES,
		TF_ROM_HEADER_CHECK_END,
		TF_ROM_HEADER_ZERO);

	for (gsize i = TF_ROM_HEADER_SKIP_BYTES; i < TF_ROM_HEADER_CHECK_END; i++) {
		if (data->data[i] != TF_ROM_HEADER_ZERO) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_FILE,
					    "invalid ROM file, non-zero data in header region");
			return FALSE;
		}
	}

	for (gsize attempt = 1; attempt <= TF_UPDATE_FLOW_MAX_ATTEMPTS; attempt++) {
		g_autoptr(GError) local_error = NULL;

		g_debug("firmware update attempt %" G_GSIZE_FORMAT "/%d",
			attempt,
			TF_UPDATE_FLOW_MAX_ATTEMPTS);

		if (fu_pxi_tp_tf_communication_write_firmware(self,
							      progress,
							      send_interval,
							      data_size,
							      data,
							      &local_error)) {
			/* read tf firmware version after successful update */
			fu_device_sleep(FU_DEVICE(self), TF_APP_VERSION_WAIT_MS);

			guint8 ver_after[3] = {0};
			g_autoptr(GError) ver_after_err = NULL;

			if (!fu_pxi_tp_tf_communication_read_firmware_version(self,
									      TF_FW_MODE_APP,
									      ver_after,
									      &ver_after_err)) {
				g_set_error(error,
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
			    fu_pxi_tp_tf_communication_version_cmp(ver_after, target_ver) != 0) {
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

			g_debug("firmware update succeeded on attempt %" G_GSIZE_FORMAT, attempt);
			return TRUE;
		}

		g_debug("firmware update attempt %" G_GSIZE_FORMAT " failed: %s",
			attempt,
			local_error != NULL ? local_error->message : "unknown error");

		if (attempt < TF_UPDATE_FLOW_MAX_ATTEMPTS)
			g_debug("retrying firmware update");
	}

	/* all attempts failed, report a single error to caller */
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_WRITE,
		    "firmware update failed after %" G_GSIZE_FORMAT " attempts",
		    (gsize)TF_UPDATE_FLOW_MAX_ATTEMPTS);
	return FALSE;
}
