/*
 * Copyright 2026 Elan Microelectronics Corporation <paul.liang@emc.com.tw>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-elan-ts-common.h"
#include "fu-elan-ts-hidraw-utility.h"
#include "fu-elan-ts-firmware.h"

/**
 * elan_ts_hidraw_get_debug_setting:
 * @device: a #FuDevice
 * 
 * Gets the debug bitmask from the firmware.
 */
guint32
elan_ts_hidraw_get_debug_setting(FuDevice *device)
{
	g_autoptr(FuFirmware) fw = NULL;

	/* Use GObject property to get the firmware object associated with this device */
	g_object_get(device, "firmware", &fw, NULL);

	if (fw != NULL && FU_IS_ELAN_TS_FIRMWARE(fw)) {
		return elan_ts_firmware_get_debug_setting(FU_ELAN_TS_FIRMWARE(fw));
	}
	return FU_ELAN_TS_DEBUG_SETTING_NONE;
}

/**
 * elan_ts_hidraw_write:
 * @device: a #FuDevice
 * @p_buf: the data buffer to write, where the first byte is the Report ID
 * @buf_len: length of @p_buf (must be <= #ELAN_TS_OUTPUT_REPORT_SIZE)
 * @timeout_ms: timeout in ms, if 0 is provided, #ELAN_TS_DEFAULT_TRANSFER_TIMEOUT_MS is used
 * @error: (nullable): a #GError, or %NULL
 *
 * Writes a raw HID Output Report to the ELAN touchscreen device once.
 * The debug setting is automatically retrieved from the associated firmware object.
 *
 * Returns: %TRUE for success, %FALSE for failure
 */
gboolean
elan_ts_hidraw_write(FuDevice *device,
		     const guint8 *p_buf,
		     gsize buf_len,
		     guint timeout_ms,
		     GError **error)
{
	FuHidrawDevice *self = FU_HIDRAW_DEVICE(device);
	g_autofree guint8 *p_out_report_buf = NULL;
	guint32 debug_setting = elan_ts_hidraw_get_debug_setting(device);

	/* basic sanity checks */
	g_return_val_if_fail(FU_IS_HIDRAW_DEVICE(device), FALSE);
	g_return_val_if_fail(p_buf != NULL, FALSE);

	/* handle default timeout */
	if (timeout_ms == 0)
		timeout_ms = ELAN_TS_DEFAULT_TRANSFER_TIMEOUT_MS;

	/* ensure the buffer length does not exceed the hardware's fixed limit */
	if (buf_len > ELAN_TS_OUTPUT_REPORT_SIZE) {
		ELAN_TS_SET_ERROR(debug_setting,
				  error,
				  FWUPD_ERROR_INVALID_DATA,
				  "failed to validate write buffer: length %u exceeds fixed report size %u",
				  (guint)buf_len,
				  (guint)ELAN_TS_OUTPUT_REPORT_SIZE);
		return FALSE;
	}

	/* prepare a zero-initialized buffer for fixed-length transmission */
	p_out_report_buf = g_malloc0(ELAN_TS_OUTPUT_REPORT_SIZE);
	memcpy(p_out_report_buf, p_buf, buf_len);

	/* Log the first 7 bytes of the outgoing report for debugging */
#if (ELAN_TS_OUTPUT_REPORT_SIZE >= 7)
	ELAN_TS_DEBUG(debug_setting,
		      "Elan TS Output Report: %02X %02X %02X %02X %02X %02X %02X ...",
		      p_out_report_buf[0], p_out_report_buf[1], p_out_report_buf[2],
		      p_out_report_buf[3], p_out_report_buf[4], p_out_report_buf[5],
		      p_out_report_buf[6]);
#endif

	/* perform a single HIDRAW set_report operation */
	if (!fu_hidraw_device_set_report(self,
					 p_out_report_buf,
					 ELAN_TS_OUTPUT_REPORT_SIZE,
					 FU_IO_CHANNEL_FLAG_NONE,
					 error)) {
		ELAN_TS_ERROR(debug_setting, error, "failed to write Elan TS Output Report: ");
		return FALSE;
	}

	return TRUE;
}

/**
 * fu_elan_ts_hid_device_write_retry_cb:
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
fu_elan_ts_hid_device_write_retry_cb(FuDevice *device, gpointer user_data, GError **error)
{
	GByteArray *buf = (GByteArray *)user_data;

	/* 
	 * Execute the base write function. If it returns FALSE, the error is 
	 * captured and fu_device_retry_full handles the retry logic.
	 */
	return elan_ts_hidraw_write(device, 
	                            buf->data, 
	                            buf->len, 
	                            0, /* use default timeout */
	                            error);
}

/**
 * elan_ts_hidraw_write_with_retry:
 * @device: a #FuDevice
 * @p_buf: raw data buffer to be sent (starting with Report ID)
 * @buf_len: the length of the buffer
 * @error: (nullable): a #GError, or %NULL
 *
 * Performs a HIDRAW write operation with an automatic retry mechanism.
 * It uses ELAN_TS_IO_MAX_RETRIES and ELAN_TS_DEFAULT_RETRY_INTERVAL_MS 
 * to manage the transaction.
 *
 * Returns: %TRUE for success, %FALSE if all retry attempts have failed
 */
gboolean
elan_ts_hidraw_write_with_retry(FuDevice *device,
                                const guint8 *p_buf,
                                gsize buf_len,
                                GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();

	/* encapsulate the raw buffer into a GByteArray for the callback */
	g_byte_array_append(buf, p_buf, buf_len);

	/* 
	 * Invoke the retry helper provided by fwupd's core.
	 * This ensures that transient I/O errors or busy states do not 
	 * cause an immediate failure of the operation.
	 */
	if (!fu_device_retry_full(device,
				  fu_elan_ts_hid_device_write_retry_cb,
				  ELAN_TS_IO_MAX_RETRIES,
				  ELAN_TS_DEFAULT_RETRY_INTERVAL_MS,
				  buf,
				  error)) {
		/* All attempts failed; the most recent error is propagated to the caller */
		return FALSE;
	}

	return TRUE;
}

/**
 * elan_ts_hidraw_read:
 * @device: a #FuDevice
 * @p_buf: destination buffer to store the read data
 * @buf_len: length of @p_buf (must be >= #ELAN_TS_INPUT_REPORT_SIZE)
 * @timeout_ms: transfer timeout in milliseconds
 * @error: (nullable): optional return location for a #GError
 *
 * Reads a fixed-size input report from the ELAN touchscreen device.
 * The function ensures the destination buffer is large enough to hold the 
 * full hardware report (65 bytes) to prevent data truncation.
 *
 * Returns: %TRUE for success, %FALSE if the read failed or buf_len is too small.
 **/
gboolean
elan_ts_hidraw_read(FuDevice *device,
		    guint8 *p_buf,
		    gsize buf_len,
		    guint timeout_ms,
		    GError **error)
{
	FuHidrawDevice *self = FU_HIDRAW_DEVICE(device);
	guint8 in_report_buf[ELAN_TS_INPUT_REPORT_SIZE] = {0x00};
	guint32 debug_setting = elan_ts_hidraw_get_debug_setting(device);

	/* Basic sanity checks */
	g_return_val_if_fail(FU_IS_HIDRAW_DEVICE(device), FALSE);
	g_return_val_if_fail(p_buf != NULL, FALSE);

	/* Ensure the provided buffer can hold the fixed hardware report size */
	if (buf_len < ELAN_TS_INPUT_REPORT_SIZE) {
		ELAN_TS_SET_ERROR(debug_setting,
				  error,
				  FWUPD_ERROR_INVALID_DATA,
				  "read buffer size %u is too small for fixed report size %u",
				  (guint)buf_len,
				  (guint)ELAN_TS_INPUT_REPORT_SIZE);
		return FALSE;
	}

	/* Set default timeout if not provided */
	if (timeout_ms == 0)
		timeout_ms = ELAN_TS_DEFAULT_TRANSFER_TIMEOUT_MS;

	/* 
	 * Read a single HIDRAW input report from the device.
	 * We always pull the full ELAN_TS_INPUT_REPORT_SIZE to ensure
	 * consistency with the hardware's expected DMA transfer size.
	 */

	if (!fu_hidraw_device_get_report(self,
				         in_report_buf, 
				         sizeof(in_report_buf),
				         FU_IO_CHANNEL_FLAG_NONE,
				         error)) {
		ELAN_TS_ERROR(debug_setting, error, "failed to read ELAN TS Input Report: ");
		return FALSE;
	}

        /* Debug */
	//fu_dump_raw(G_LOG_DOMAIN, "ELAN TS Input Report", in_report_buf, sizeof(in_report_buf));

	/* Copy the full report to the provided buffer */
	memcpy(p_buf, in_report_buf, ELAN_TS_INPUT_REPORT_SIZE);

	/* Debug logging: print the first 6 bytes for protocol analysis */
#if (ELAN_TS_INPUT_REPORT_SIZE >= 6)
	ELAN_TS_DEBUG(debug_setting,
		      "Elan TS Input Report: %02X %02X %02X %02X %02X %02X ...",
		      in_report_buf[0], in_report_buf[1], in_report_buf[2],
		      in_report_buf[3], in_report_buf[4], in_report_buf[5]);
#endif

	return TRUE;
}

/**
 * fu_elan_ts_hid_device_read_retry_cb:
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
fu_elan_ts_hid_device_read_retry_cb(FuDevice *device, gpointer user_data, GError **error)
{
	GByteArray *buf = (GByteArray *)user_data;

	/* 
	 * Execute the base read function. 
	 * If the hardware does not return data within the timeout, or if the
	 * read fails, fu_device_retry_full will handle the next attempt.
	 */
	return elan_ts_hidraw_read(device, 
	                           buf->data, 
	                           buf->len, 
	                           0, /* use default timeout */
	                           error);
}

/**
 * elan_ts_hidraw_read_with_retry:
 * @device: a #FuDevice
 * @p_buf: buffer to store the incoming report (starting with Report ID)
 * @buf_len: the expected length of the report (usually ELAN_TS_INPUT_REPORT_SIZE)
 * @error: (nullable): a #GError, or %NULL
 *
 * Reads a HID report from the ELAN device with an automatic retry mechanism.
 * It uses ELAN_TS_IO_MAX_RETRIES and ELAN_TS_DEFAULT_RETRY_INTERVAL_MS 
 * to ensure robust communication.
 *
 * Returns: %TRUE for success, %FALSE if all retry attempts have failed
 */
gboolean
elan_ts_hidraw_read_with_retry(FuDevice *device,
                               guint8 *p_buf,
                               gsize buf_len,
                               GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_byte_array_set_size(buf, buf_len);
	memset(buf->data, 0, buf->len);

	/* 
	 * Invoke the retry helper. This is critical for ELAN I2C devices to
	 * successfully capture the response when the host's HID driver is
	 * concurrently polling the device.
	 */
	if (!fu_device_retry_full(device,
				  fu_elan_ts_hid_device_read_retry_cb,
				  ELAN_TS_IO_MAX_RETRIES,
				  ELAN_TS_DEFAULT_RETRY_INTERVAL_MS,
				  buf,
				  error)) {
		return FALSE;
	}

	/* Copy the successfully read data back to the caller's buffer */
	memcpy(p_buf, buf->data, buf_len);

	return TRUE;
}
