/*
 * Copyright 2026 Elan Microelectronics Corporation <paul.liang@emc.com.tw>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupd-plugin.h>
#include <libfwupdplugin/fu-hid-device.h>

/**
 * elan_ts_hidraw_get_debug_setting:
 * @device: a #FuDevice
 * 
 * Gets the debug bitmask from the firmware.
 */
guint32
elan_ts_hidraw_get_debug_setting(FuDevice *device);

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
		     GError **error);

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
                                GError **error);

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
		    GError **error);

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
                               GError **error);

