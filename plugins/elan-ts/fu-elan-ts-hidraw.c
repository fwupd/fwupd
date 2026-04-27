/*
 * Copyright 2026 Elan Microelectronics Corporation <paul.liang@emc.com.tw>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-elan-ts-common.h"
#include "fu-elan-ts-firmware.h"
#include "fu-elan-ts-hidraw.h"

/**
 * fu_elan_ts_hidraw_write:
 * @device: a #FuHidrawDevice
 * @p_buf: the data buffer to write, where the first byte is the Report ID
 * @buf_len: length of @p_buf (must be <= #ELAN_TS_OUTPUT_REPORT_SIZE)
 * @error: (nullable): a #GError, or %NULL
 *
 * Writes a raw HID Output Report to the ELAN touchscreen device once.
 * The debug setting is automatically retrieved from the associated firmware object.
 *
 * Returns: %TRUE for success, %FALSE for failure
 */
static gboolean
fu_elan_ts_hidraw_write(FuHidrawDevice *device, const guint8 *p_buf, gsize buf_len, GError **error)
{
	g_autofree guint8 *p_out_report_buf = NULL;

	/* basic sanity checks */
	g_return_val_if_fail(FU_IS_HIDRAW_DEVICE(device), FALSE);
	g_return_val_if_fail(p_buf != NULL, FALSE);
	g_return_val_if_fail((error == NULL) || (*error == NULL), FALSE);

	/* ensure the buffer length does not exceed the hardware's fixed limit */
	if (buf_len > ELAN_TS_OUTPUT_REPORT_SIZE) {
		g_set_error(
		    error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_INVALID_DATA,
		    "failed to validate write buffer: length %u exceeds fixed report size %u",
		    (guint)buf_len,
		    (guint)ELAN_TS_OUTPUT_REPORT_SIZE);
		return FALSE;
	}

	/* prepare a zero-initialized buffer for fixed-length transmission */
	p_out_report_buf = g_malloc0(ELAN_TS_OUTPUT_REPORT_SIZE);
	if (!fu_memcpy_safe(p_out_report_buf,
			    ELAN_TS_OUTPUT_REPORT_SIZE,
			    0, /* dst */
			    p_buf,
			    buf_len,
			    0, /* src */
			    buf_len,
			    error)) {
		g_prefix_error_literal(error, "failed to copy report data: ");
		return FALSE;
	}

	/* log the first 7 bytes of the outgoing report for debugging */
#if (ELAN_TS_OUTPUT_REPORT_SIZE >= 7)
	g_debug("elan ts output report: %02x %02x %02x %02x %02x %02x %02x",
		p_out_report_buf[0],
		p_out_report_buf[1],
		p_out_report_buf[2],
		p_out_report_buf[3],
		p_out_report_buf[4],
		p_out_report_buf[5],
		p_out_report_buf[6]);
#endif

	/* perform a single HIDRAW set_report operation */
	if (!fu_hidraw_device_set_report(device,
					 p_out_report_buf,
					 ELAN_TS_OUTPUT_REPORT_SIZE,
					 FU_IO_CHANNEL_FLAG_NONE,
					 error)) {
		g_prefix_error_literal(error, "failed to write Elan TS Output Report: ");
		return FALSE;
	}

	return TRUE;
}

/**
 * fu_elan_ts_hidraw_write_retry_cb:
 * @device: a #FuDevice
 * @user_data: a #GByteArray containing the output report data
 * @error: (nullable): a #GError
 *
 * The callback function used by fu_device_retry_full to perform a single
 * HIDRAW write attempt.
 *
 * Returns: %TRUE for success, %FALSE for failure (which triggers a retry)
 */
static gboolean
fu_elan_ts_hidraw_write_retry_cb(FuDevice *device, gpointer user_data, GError **error)
{
	GByteArray *buf = (GByteArray *)user_data;

	/* basic sanity checks */
	g_return_val_if_fail(FU_IS_HIDRAW_DEVICE(device), FALSE);
	g_return_val_if_fail(buf != NULL, FALSE);
	g_return_val_if_fail((error == NULL) || (*error == NULL), FALSE);

	/*
	 * Execute the base write function. If it returns FALSE, the error is
	 * captured and fu_device_retry_full handles the retry logic.
	 */
	return fu_elan_ts_hidraw_write(FU_HIDRAW_DEVICE(device), buf->data, buf->len, error);
}

/**
 * fu_elan_ts_hidraw_write_with_retry:
 * @device: a #FuHidrawDevice
 * @p_buf: raw data buffer to be sent (starting with Report ID)
 * @buf_len: the length of the buffer
 * @error: (nullable): a #GError, or %NULL
 *
 * Performs a HIDRAW write operation with an automatic retry mechanism.
 *
 * Returns: %TRUE for success, %FALSE if all retry attempts have failed
 */
gboolean
fu_elan_ts_hidraw_write_with_retry(FuHidrawDevice *device,
				   const guint8 *p_buf,
				   gsize buf_len,
				   GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();

	/* basic sanity checks */
	g_return_val_if_fail(FU_IS_HIDRAW_DEVICE(device), FALSE);
	g_return_val_if_fail(p_buf != NULL, FALSE);
	g_return_val_if_fail((error == NULL) || (*error == NULL), FALSE);

	/* encapsulate the raw buffer into a GByteArray for the callback */
	g_byte_array_append(buf, p_buf, buf_len);

	/*
	 * Invoke the retry helper provided by fwupd's core.
	 * This ensures that transient I/O errors or busy states do not
	 * cause an immediate failure of the operation.
	 */
	if (!fu_device_retry_full(FU_DEVICE(device),
				  fu_elan_ts_hidraw_write_retry_cb,
				  ELAN_TS_IO_MAX_RETRIES,
				  0,
				  buf,
				  error))
		return FALSE;

	return TRUE;
}

/**
 * fu_elan_ts_hidraw_read:
 * @device: a #FuHidrawDevice
 * @p_buf: destination buffer to store the read data
 * @buf_len: length of @p_buf (must be >= #ELAN_TS_INPUT_REPORT_SIZE)
 * @error: (nullable): optional return location for a #GError
 *
 * Reads a fixed-size input report from the ELAN touchscreen device.
 * The function ensures the destination buffer is large enough to hold the
 * full hardware report (65 bytes) to prevent data truncation.
 *
 * Returns: %TRUE for success, %FALSE if the read failed or buf_len is too small.
 **/
static gboolean
fu_elan_ts_hidraw_read(FuHidrawDevice *device, guint8 *p_buf, gsize buf_len, GError **error)
{
	guint8 in_report_buf[ELAN_TS_INPUT_REPORT_SIZE] = {0x00};

	/* basic sanity checks */
	g_return_val_if_fail(FU_IS_HIDRAW_DEVICE(device), FALSE);
	g_return_val_if_fail(p_buf != NULL, FALSE);
	g_return_val_if_fail((error == NULL) || (*error == NULL), FALSE);

	/* ensure the provided buffer can hold the fixed hardware report size */
	if (buf_len < ELAN_TS_INPUT_REPORT_SIZE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "read buffer size %u is too small for fixed report size %u",
			    (guint)buf_len,
			    (guint)ELAN_TS_INPUT_REPORT_SIZE);
		return FALSE;
	}

	/*
	 * Read a single HIDRAW input report from the device.
	 * We always pull the full ELAN_TS_INPUT_REPORT_SIZE to ensure
	 * consistency with the hardware's expected DMA transfer size.
	 */

	if (!fu_hidraw_device_get_report(device,
					 in_report_buf,
					 sizeof(in_report_buf),
					 FU_IO_CHANNEL_FLAG_NONE,
					 error)) {
		g_prefix_error_literal(error, "failed to read ELAN TS Input Report: ");
		return FALSE;
	}

	/* copy the full report to the provided buffer */
	if (!fu_memcpy_safe(p_buf, /* dst */
			    buf_len,
			    0,
			    in_report_buf, /* src */
			    sizeof(in_report_buf),
			    0,
			    sizeof(in_report_buf),
			    error)) {
		g_prefix_error_literal(error, "failed to copy input report: ");
		return FALSE;
	}

	/* debug logging: print the first 6 bytes for protocol analysis */
#if (ELAN_TS_INPUT_REPORT_SIZE >= 6)
	g_debug("elan ts input report: %02x %02x %02x %02x %02x %02x",
		in_report_buf[0],
		in_report_buf[1],
		in_report_buf[2],
		in_report_buf[3],
		in_report_buf[4],
		in_report_buf[5]);
#endif

	return TRUE;
}

/**
 * fu_elan_ts_hidraw_read_retry_cb:
 * @device: a #FuDevice
 * @user_data: a #GByteArray to store the received input report
 * @error: (nullable): a #GError
 *
 * The callback function used by fu_device_retry_full to perform a single
 * HIDRAW read attempt.
 *
 * Returns: %TRUE for success, %FALSE for failure (which triggers a retry)
 */
static gboolean
fu_elan_ts_hidraw_read_retry_cb(FuDevice *device, gpointer user_data, GError **error)
{
	GByteArray *buf = (GByteArray *)user_data;

	/* basic sanity checks */
	g_return_val_if_fail(FU_IS_HIDRAW_DEVICE(device), FALSE);
	g_return_val_if_fail(buf != NULL, FALSE);
	g_return_val_if_fail((error == NULL) || (*error == NULL), FALSE);

	/*
	 * Execute the base read function.
	 * If the hardware does not return data within the timeout, or if the
	 * read fails, fu_device_retry_full will handle the next attempt.
	 */
	return fu_elan_ts_hidraw_read(FU_HIDRAW_DEVICE(device), buf->data, buf->len, error);
}

/**
 * fu_elan_ts_hidraw_read_with_retry:
 * @device: a #FuHidrawDevice
 * @p_buf: buffer to store the incoming report (starting with Report ID)
 * @buf_len: the expected length of the report (usually ELAN_TS_INPUT_REPORT_SIZE)
 * @error: (nullable): a #GError, or %NULL
 *
 * Reads a HID report from the ELAN device with an automatic retry mechanism.
 *
 * Returns: %TRUE for success, %FALSE if all retry attempts have failed
 */
gboolean
fu_elan_ts_hidraw_read_with_retry(FuHidrawDevice *device,
				  guint8 *p_buf,
				  gsize buf_len,
				  GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();

	/* basic sanity checks */
	g_return_val_if_fail(FU_IS_HIDRAW_DEVICE(device), FALSE);
	g_return_val_if_fail(p_buf != NULL, FALSE);
	g_return_val_if_fail(buf_len != 0, FALSE);
	g_return_val_if_fail((error == NULL) || (*error == NULL), FALSE);

	g_byte_array_set_size(buf, buf_len);
	memset(buf->data, 0, buf->len);

	/*
	 * Invoke the retry helper. This is critical for ELAN I2C devices to
	 * successfully capture the response when the host's HID driver is
	 * concurrently polling the device.
	 */
	if (!fu_device_retry_full(FU_DEVICE(device),
				  fu_elan_ts_hidraw_read_retry_cb,
				  ELAN_TS_IO_MAX_RETRIES,
				  0,
				  buf,
				  error))
		return FALSE;

	/* copy the successfully read data back to the caller's buffer */
	if (!fu_memcpy_safe(p_buf, /* dst */
			    buf_len,
			    0,
			    buf->data, /* src */
			    buf->len,
			    0,
			    buf_len,
			    error)) {
		g_prefix_error_literal(error, "failed to copy read data back: ");
		return FALSE;
	}

	return TRUE;
}
