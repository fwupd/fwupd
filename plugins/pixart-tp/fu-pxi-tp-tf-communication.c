#include "config.h"

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

/* CRC8: x^8 + x^2 + x + 1 */
static const guint8 CRC8_TABLE[256] = /* nocheck:static */ {
    0x00, 0x07, 0x0E, 0x09, 0x1C, 0x1B, 0x12, 0x15, 0x38, 0x3F, 0x36, 0x31, 0x24, 0x23, 0x2A, 0x2D,
    0x70, 0x77, 0x7E, 0x79, 0x6C, 0x6B, 0x62, 0x65, 0x48, 0x4F, 0x46, 0x41, 0x54, 0x53, 0x5A, 0x5D,
    0xE0, 0xE7, 0xEE, 0xE9, 0xFC, 0xFB, 0xF2, 0xF5, 0xD8, 0xDF, 0xD6, 0xD1, 0xC4, 0xC3, 0xCA, 0xCD,
    0x90, 0x97, 0x9E, 0x99, 0x8C, 0x8B, 0x82, 0x85, 0xA8, 0xAF, 0xA6, 0xA1, 0xB4, 0xB3, 0xBA, 0xBD,
    0xC7, 0xC0, 0xC9, 0xCE, 0xDB, 0xDC, 0xD5, 0xD2, 0xFF, 0xF8, 0xF1, 0xF6, 0xE3, 0xE4, 0xED, 0xEA,
    0xB7, 0xB0, 0xB9, 0xBE, 0xAB, 0xAC, 0xA5, 0xA2, 0x8F, 0x88, 0x81, 0x86, 0x93, 0x94, 0x9D, 0x9A,
    0x27, 0x20, 0x29, 0x2E, 0x3B, 0x3C, 0x35, 0x32, 0x1F, 0x18, 0x11, 0x16, 0x03, 0x04, 0x0D, 0x0A,
    0x57, 0x50, 0x59, 0x5E, 0x4B, 0x4C, 0x45, 0x42, 0x6F, 0x68, 0x61, 0x66, 0x73, 0x74, 0x7D, 0x7A,
    0x89, 0x8E, 0x87, 0x80, 0x95, 0x92, 0x9B, 0x9C, 0xB1, 0xB6, 0xBF, 0xB8, 0xAD, 0xAA, 0xA3, 0xA4,
    0xF9, 0xFE, 0xF7, 0xF0, 0xE5, 0xE2, 0xEB, 0xEC, 0xC1, 0xC6, 0xCF, 0xC8, 0xDD, 0xDA, 0xD3, 0xD4,
    0x69, 0x6E, 0x67, 0x60, 0x75, 0x72, 0x7B, 0x7C, 0x51, 0x56, 0x5F, 0x58, 0x4D, 0x4A, 0x43, 0x44,
    0x19, 0x1E, 0x17, 0x10, 0x05, 0x02, 0x0B, 0x0C, 0x21, 0x26, 0x2F, 0x28, 0x3D, 0x3A, 0x33, 0x34,
    0x4E, 0x49, 0x40, 0x47, 0x52, 0x55, 0x5C, 0x5B, 0x76, 0x71, 0x78, 0x7F, 0x6A, 0x6D, 0x64, 0x63,
    0x3E, 0x39, 0x30, 0x37, 0x22, 0x25, 0x2C, 0x2B, 0x06, 0x01, 0x08, 0x0F, 0x1A, 0x1D, 0x14, 0x13,
    0xAE, 0xA9, 0xA0, 0xA7, 0xB2, 0xB5, 0xBC, 0xBB, 0x96, 0x91, 0x98, 0x9F, 0x8A, 0x8D, 0x84, 0x83,
    0xDE, 0xD9, 0xD0, 0xD7, 0xC2, 0xC5, 0xCC, 0xCB, 0xE6, 0xE1, 0xE8, 0xEF, 0xFA, 0xFD, 0xF4, 0xF3,
};

static guint8
fu_pxi_tp_tf_communication_compute_crc8(const guint8 *data, gsize len)
{
	guint8 crc = 0;

	while (len--)
		crc = CRC8_TABLE[crc ^ *data++];

	return crc;
}

/* --- TF Standard Communication helpers --- */

static gboolean
fu_pxi_tp_tf_communication_write_rmi_cmd(FuPxiTpDevice *self,
					 guint16 addr,
					 const guint8 *in_buf,
					 gsize in_bufsz,
					 GError **error)
{
	guint8 buf[TF_FEATURE_REPORT_BYTE_LENGTH] = {0};
	gsize offset = 0;

	buf[offset++] = REPORT_ID__PASS_THROUGH;
	buf[offset++] = 0x5A;
	buf[offset++] = SLAVE_ADDRESS;
	buf[offset++] = 0x00; /* function code */
	buf[offset++] = addr & 0xFF;
	buf[offset++] = addr >> 8;
	buf[offset++] = in_bufsz & 0xFF;
	buf[offset++] = in_bufsz >> 8;

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
	}

	offset += in_bufsz;
	buf[offset++] = fu_pxi_tp_tf_communication_compute_crc8(buf + 2, offset - 2);
	buf[offset++] = 0xA5;

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

	buf[offset++] = REPORT_ID__PASS_THROUGH;
	buf[offset++] = 0x5A;
	buf[offset++] = SLAVE_ADDRESS;
	buf[offset++] = 0x04; /* function code with packet info */
	buf[offset++] = addr & 0xFF;
	buf[offset++] = addr >> 8;

	buf[offset++] = datalen & 0xFF;
	buf[offset++] = datalen >> 8;

	buf[offset++] = packet_total & 0xFF;
	buf[offset++] = packet_total >> 8;
	buf[offset++] = packet_index & 0xFF;
	buf[offset++] = packet_index >> 8;

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
	}

	offset += in_bufsz;
	buf[offset++] = fu_pxi_tp_tf_communication_compute_crc8(buf + 2, offset - 2);
	buf[offset++] = 0xA5;

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

	out_buf[offset++] = REPORT_ID__PASS_THROUGH;
	out_buf[offset++] = 0x5A;
	out_buf[offset++] = SLAVE_ADDRESS;
	out_buf[offset++] = 0x0B; /* function code with reply length */
	out_buf[offset++] = addr & 0xFF;
	out_buf[offset++] = addr >> 8;

	/* nDataLen = input length + 2 bytes for reply length (low/high) */
	datalen = in_bufsz + 2;
	out_buf[offset++] = datalen & 0xFF;
	out_buf[offset++] = datalen >> 8;

	/* reply length hint: expected payload length */
	if (n_bytes_returned != NULL) {
		out_buf[offset++] = (guint8)(*n_bytes_returned & 0xFF);
		out_buf[offset++] = (guint8)((*n_bytes_returned >> 8) & 0xFF);
	} else {
		out_buf[offset++] = 0x00;
		out_buf[offset++] = 0x00;
	}

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
	}

	offset += in_bufsz;
	out_buf[offset++] = fu_pxi_tp_tf_communication_compute_crc8(out_buf + 2, offset - 2);
	out_buf[offset++] = 0xA5;

	if (!fu_pxi_tp_common_send_feature(self, out_buf, TF_FEATURE_REPORT_BYTE_LENGTH, error))
		return FALSE;

	fu_device_sleep(FU_DEVICE(self), 10);

	if (!fu_pxi_tp_common_get_feature(self,
					  REPORT_ID__PASS_THROUGH,
					  out_buf,
					  TF_FEATURE_REPORT_BYTE_LENGTH,
					  error))
		return FALSE;

	/* parse reply header */
	if (out_buf[1] != 0x5A || out_buf[2] != SLAVE_ADDRESS) {
		return fu_pxi_tp_common_fail(error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_WRITE,
					     "TF RMI read: invalid header 0x%02x 0x%02x",
					     out_buf[1],
					     out_buf[2]);
	}

	/* exception frame? */
	if ((out_buf[3] & 0x80) != 0) {
		return fu_pxi_tp_common_fail(error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_WRITE,
					     "TF RMI read: device returned exception 0x%02x",
					     out_buf[3]);
	}

	/* datalen is payload length reported by device */
	datalen = out_buf[4] + ((gsize)out_buf[5] << 8);
	if (fu_pxi_tp_tf_communication_compute_crc8(out_buf + 2, datalen + 4) !=
		out_buf[datalen + 6] ||
	    out_buf[datalen + 7] != 0xA5) {
		return fu_pxi_tp_common_fail(error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_WRITE,
					     "TF RMI read: CRC or tail mismatch");
	}

	if (n_bytes_returned != NULL)
		*n_bytes_returned = datalen + 8;

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
	gsize len = 3; /* expected payload length = 3 bytes (version) */

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
					    3, /* dst_sz */
					    0, /* dst_offset */
					    in_buf,
					    sizeof(in_buf), /* src_sz */
					    6,		    /* src_offset */
					    3,		    /* n */
					    error))
				return FALSE;

			return TRUE;
		}

		g_debug("TF: read firmware version failed try %" G_GSIZE_FORMAT "/%d: %s",
			i + 1,
			FAILED_RETRY_TIMES,
			local_error != NULL ? local_error->message : "unknown");

		if (i < FAILED_RETRY_TIMES - 1)
			fu_device_sleep(FU_DEVICE(self), FAILED_RETRY_INTERVAL);
	}

	return fu_pxi_tp_common_fail(error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_WRITE,
				     "TF: failed to read firmware version after %d retries",
				     FAILED_RETRY_TIMES);
}

/* Read TF upgrade download status (status + number of packets accepted by MCU) */
static gboolean
fu_pxi_tp_tf_communication_read_download_status(FuPxiTpDevice *self,
						guint8 *status,
						guint16 *packet_number,
						GError **error)
{
	guint8 in_buf[TF_FEATURE_REPORT_BYTE_LENGTH] = {0};
	gsize len = 3; /* expected payload length = 1 (status) + 2 (packet_number) */

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
			if (len >= 11 && len - 8 == 3) {
				*status = in_buf[6];
				*packet_number = (guint16)in_buf[7] + ((guint16)in_buf[8] << 8);
				return TRUE;
			}

			g_debug("TF: download status reply has unexpected length: %" G_GSIZE_FORMAT,
				len);
			/* treat as failure and retry */
		}

		g_debug("TF: read download status failed try %" G_GSIZE_FORMAT "/%d: %s",
			i + 1,
			FAILED_RETRY_TIMES,
			local_error != NULL ? local_error->message : "unknown");

		if (i < FAILED_RETRY_TIMES - 1)
			fu_device_sleep(FU_DEVICE(self), FAILED_RETRY_INTERVAL);
	}

	return fu_pxi_tp_common_fail(error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_WRITE,
				     "TF: failed to read download status after %d retries",
				     FAILED_RETRY_TIMES);
}

/* Perform one TF firmware update attempt: no outer retries here */
static gboolean
fu_pxi_tp_tf_communication_write_firmware(FuPxiTpDevice *self,
					  FuProgress *progress,
					  guint32 send_interval,
					  guint32 data_size,
					  const GByteArray *data,
					  GError **error)
{
	const guint32 max_packet_data_len = 32;
	const guint32 erase_wait_time = 2000; /* ms */

	(void)progress; /* currently unused, can be wired to progress if needed */

	/* Disable touch function while updating TF */
	g_debug("TF: disabling touch");
	guint8 touch_operate_buf = 1;
	if (!fu_pxi_tp_tf_communication_write_rmi_cmd(self,
						      TOUCH_CONTROL,
						      &touch_operate_buf,
						      1,
						      error)) {
		return fu_pxi_tp_common_fail(error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_WRITE,
					     "TF: failed to disable touch");
	}

	/* Enter TF bootloader / upgrade mode */
	g_debug("TF: enter bootloader mode");
	guint8 tmp_buf[TF_FEATURE_REPORT_BYTE_LENGTH] = {0};
	tmp_buf[0] = 0x01;
	if (!fu_pxi_tp_tf_communication_write_rmi_cmd(self, SET_UPGRADE_MODE, tmp_buf, 1, error)) {
		/* best-effort rollback: re-enable touch, ignore errors */
		touch_operate_buf = 0;
		g_debug("TF: re-enabling touch after bootloader enter failure");
		(void)fu_pxi_tp_tf_communication_write_rmi_cmd(self,
							       TOUCH_CONTROL,
							       &touch_operate_buf,
							       1,
							       NULL);

		return fu_pxi_tp_common_fail(error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_WRITE,
					     "TF: failed to enter bootloader mode");
	}

	fu_device_sleep(FU_DEVICE(self), 100);

	/* Erase flash before programming */
	g_debug("TF: erase flash");
	tmp_buf[0] = 0x02; /* erase mode */
	if (!fu_pxi_tp_tf_communication_write_rmi_cmd(self, SET_UPGRADE_MODE, tmp_buf, 1, error)) {
		touch_operate_buf = 0;
		g_debug("TF: re-enabling touch after erase command failure");
		(void)fu_pxi_tp_tf_communication_write_rmi_cmd(self,
							       TOUCH_CONTROL,
							       &touch_operate_buf,
							       1,
							       NULL);

		return fu_pxi_tp_common_fail(error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_WRITE,
					     "TF: failed to send erase flash command");
	}

	fu_device_sleep(FU_DEVICE(self), erase_wait_time);

	/* Calculate total number of packets */
	guint32 num = (data_size + max_packet_data_len - 1) / max_packet_data_len;
	guint32 offset = 0;

	g_debug("TF: start writing flash, packets=%u, total_size=%u", num, data_size);

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

			g_debug("TF: packet %u write failed, attempt %u/%d: %s",
				i,
				k + 1,
				FAILED_RETRY_TIMES,
				local_error != NULL ? local_error->message : "unknown");

			if (k < FAILED_RETRY_TIMES - 1)
				fu_device_sleep(FU_DEVICE(self),
						send_interval > 0 ? send_interval : 50);
		}

		if (k == FAILED_RETRY_TIMES) {
			/* give up on this packet: rollback touch and fail */
			touch_operate_buf = 0;
			g_debug("TF: re-enabling touch after packet %u write failure", i);
			(void)fu_pxi_tp_tf_communication_write_rmi_cmd(self,
								       TOUCH_CONTROL,
								       &touch_operate_buf,
								       1,
								       NULL);

			return fu_pxi_tp_common_fail(
			    error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "TF: failed to write flash packet %u after %d retries",
			    i,
			    FAILED_RETRY_TIMES);
		}

		/* small delay between packets */
		if (send_interval > 0)
			fu_device_sleep(FU_DEVICE(self), send_interval);
	}

	g_debug("TF: all packets sent, checking download status");

	/* Read back download status from device */
	guint8 status = 0;
	guint16 mcu_packet_number = 0;
	if (!fu_pxi_tp_tf_communication_read_download_status(self,
							     &status,
							     &mcu_packet_number,
							     error)) {
		touch_operate_buf = 0;
		g_debug("TF: re-enabling touch after download status read failure");
		(void)fu_pxi_tp_tf_communication_write_rmi_cmd(self,
							       TOUCH_CONTROL,
							       &touch_operate_buf,
							       1,
							       NULL);

		return fu_pxi_tp_common_fail(error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_WRITE,
					     "TF: failed to read download status");
	}

	g_debug("TF: download status OK, expected_packets=%u, device_packets=%u, status=%u",
		num,
		mcu_packet_number,
		status);

	if (status != 0 || mcu_packet_number != num) {
		touch_operate_buf = 0;
		g_debug("TF: re-enabling touch after download status mismatch");
		(void)fu_pxi_tp_tf_communication_write_rmi_cmd(self,
							       TOUCH_CONTROL,
							       &touch_operate_buf,
							       1,
							       NULL);

		return fu_pxi_tp_common_fail(
		    error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_WRITE,
		    "TF: upgrade failed, status=%u, device_packets=%u, expected_packets=%u",
		    status,
		    mcu_packet_number,
		    num);
	}

	fu_device_sleep(FU_DEVICE(self), 50);
	g_debug("TF: download status indicates success, exiting upgrade mode");

	/* Exit upgrade mode (best-effort) */
	tmp_buf[0] = 0x00;
	if (!fu_pxi_tp_tf_communication_write_rmi_cmd(self, SET_UPGRADE_MODE, tmp_buf, 1, NULL))
		g_debug("TF: failed to exit upgrade mode (ignored)");

	/* Re-enable touch (best-effort) */
	touch_operate_buf = 0;
	g_debug("TF: re-enabling touch after successful upgrade");
	if (!fu_pxi_tp_tf_communication_write_rmi_cmd(self,
						      TOUCH_CONTROL,
						      &touch_operate_buf,
						      1,
						      NULL))
		g_debug("TF: failed to re-enable touch (ignored)");

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
 * - reads TF version before update (mode=1, APP)
 * - skips update if current version >= target version
 * - validates TF image
 * - stops touchpad report
 * - retries the TF update a few times at high level
 * - reads TF version after successful update
 * - verifies TF version matches target version
 */
gboolean
fu_pxi_tp_tf_communication_write_firmware_process(FuDevice *device,
						  FuProgress *progress,
						  guint32 send_interval,
						  guint32 data_size,
						  GByteArray *data,
						  const guint8 target_ver[3],
						  GError **error)
{
	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(device);
	const gsize max_attempts = 3;
	guint8 ver_before[3] = {0};
	gboolean have_ver_before = FALSE;
	g_autoptr(GError) ver_err = NULL;

	fu_device_sleep(FU_DEVICE(self), 1000);

	/* Stop touchpad reports while updating TF */
	g_debug("TF: stop touchpad report");
	WRITE_USER_REG(0x00, 0x56, 0x01);

	/* Try to read TF firmware version before update (mode=1: APP) */
	if (fu_pxi_tp_tf_communication_read_firmware_version(self,
							     1, /* APP */
							     ver_before,
							     &ver_err)) {
		g_debug("TF: firmware version before update (mode=1 APP): %u.%u.%u",
			ver_before[0],
			ver_before[1],
			ver_before[2]);
		have_ver_before = TRUE;

		/* If current version >= target, skip update */
		if (target_ver != NULL &&
		    fu_pxi_tp_tf_communication_version_cmp(ver_before, target_ver) >= 0) {
			g_debug("TF: current FW %u.%u.%u >= target %u.%u.%u, skip update",
				ver_before[0],
				ver_before[1],
				ver_before[2],
				target_ver[0],
				target_ver[1],
				target_ver[2]);
			return TRUE;
		}
	} else {
		g_debug("TF: failed to read firmware version before update: %s",
			ver_err != NULL ? ver_err->message : "unknown error");
	}

	/* Sanity check: first 128 bytes (except some header) must be zero */
	g_debug("TF: validate ROM header (first 128 bytes after header must be zero)");
	for (gsize i = 6; i < 128; i++) {
		if (data->data[i] != 0x00) {
			return fu_pxi_tp_common_fail(
			    error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "TF: invalid ROM file, non-zero data in header region");
		}
	}

	for (gsize attempt = 1; attempt <= max_attempts; attempt++) {
		g_autoptr(GError) local_error = NULL;

		g_debug("TF: firmware update attempt %" G_GSIZE_FORMAT "/%" G_GSIZE_FORMAT,
			attempt,
			max_attempts);

		if (fu_pxi_tp_tf_communication_write_firmware(self,
							      progress,
							      send_interval,
							      data_size,
							      data,
							      &local_error)) {
			/* Read TF firmware version after successful update */
			fu_device_sleep(FU_DEVICE(self), 1000);

			guint8 ver_after[3] = {0};
			g_autoptr(GError) ver_after_err = NULL;

			if (!fu_pxi_tp_tf_communication_read_firmware_version(self,
									      1, /* APP */
									      ver_after,
									      &ver_after_err)) {
				return fu_pxi_tp_common_fail(
				    error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_READ,
				    "TF: failed to read firmware version after update: %s",
				    ver_after_err != NULL ? ver_after_err->message
							  : "unknown error");
			}

			g_debug("TF: firmware version after update (mode=1 APP): %u.%u.%u",
				ver_after[0],
				ver_after[1],
				ver_after[2]);

			/* Verify version matches target version */
			if (target_ver != NULL &&
			    fu_pxi_tp_tf_communication_version_cmp(ver_after, target_ver) != 0) {
				return fu_pxi_tp_common_fail(
				    error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "TF: firmware version after update (%u.%u.%u) "
				    "does not match target (%u.%u.%u)",
				    ver_after[0],
				    ver_after[1],
				    ver_after[2],
				    target_ver[0],
				    target_ver[1],
				    target_ver[2]);
			}

			g_debug("TF: firmware update succeeded on attempt %" G_GSIZE_FORMAT,
				attempt);
			return TRUE;
		}

		g_debug("TF: firmware update attempt %" G_GSIZE_FORMAT " failed: %s",
			attempt,
			local_error != NULL ? local_error->message : "unknown error");

		if (attempt < max_attempts)
			g_debug("TF: retrying firmware update");
	}

	/* All attempts failed, report a single error to caller */
	return fu_pxi_tp_common_fail(error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_WRITE,
				     "TF: firmware update failed after %" G_GSIZE_FORMAT
				     " attempts",
				     max_attempts);
}
