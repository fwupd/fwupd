/*
 * Copyright 2026 Elan Microelectronics Corporation <paul.liang@emc.com.tw>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#pragma once

#include <fwupdplugin.h>
#include <libfwupdplugin/fu-hid-device.h>

/**
 * elan_ts_hid_write_vendor_command:
 * @device: a #FuDevice
 * @p_vendor_cmd_buf: the raw vendor command payload (without Report ID)
 * @vendor_cmd_len: length of @p_vendor_cmd_buf
 * @error: (nullable): a #GError, or %NULL
 *
 * Wraps the vendor command payload with the HID Output Report ID (0x03) 
 * and sends it as a fixed-length report (33 bytes) with retries.
 *
 * Returns: %TRUE for success, %FALSE for failure
 */
gboolean
elan_ts_hid_write_vendor_command(FuDevice *device,
				 const guint8 *p_vendor_cmd_buf,
				 gsize vendor_cmd_len,
				 GError **error);

/**
 * elan_ts_hid_write_command:
 * @device: a #FuDevice
 * @p_cmd_buf: the raw command payload (e.g., 4 or 6 bytes)
 * @cmd_len: length of @p_cmd_buf
 * @error: (nullable): a #GError, or %NULL
 *
 * Wraps the TP command with the appropriate HID Report ID (based on PID) 
 * and a 2-byte header, then sends it as a fixed-length HID Output Report with retries.
 * The timeout and retry logic are managed internally by the retry framework.
 *
 * Returns: %TRUE for success, %FALSE for failure
 */
gboolean
elan_ts_hid_write_command(FuDevice *device,
			  const guint8 *p_cmd_buf,
			  gsize cmd_len,
			  GError **error);

/**
 * elan_ts_hid_read_data:
 * @device: a #FuDevice
 * @p_data_buf: (out): destination buffer to store the read data
 * @data_len: length of data to copy into @p_data_buf
 * @filter: if %TRUE, strips the 2-byte HID header (Report ID + Status/Length)
 * @error: (nullable): a #GError, or %NULL
 *
 * Reads an input report from ELAN TS with retries and validates the Report ID.
 * The timeout and retry logic are managed internally.
 *
 * Returns: %TRUE for success, %FALSE for failure or invalid data pattern.
 */
gboolean
elan_ts_hid_read_data(FuDevice *device,
		      guint8 *p_data_buf,
		      gsize data_len,
		      gboolean filter,
		      GError **error);

/**
 * elan_ts_hid_read_hello_packet_bc_version_with_retry:
 * @device: a #FuDevice
 * @p_hello_packet: (out): stores the hello packet value (e.g., 0x20 or 0x56)
 * @p_bc_version: (out): stores the 16-bit boot code version
 * @error: (nullable): a #GError, or %NULL
 *
 * Requests the hello packet and BC version with an automatic retry mechanism.
 * This function uses ELAN_TS_IO_MAX_RETRIES internally to manage multiple
 * attempts, ensuring the device has sufficient time to respond.
 *
 * Returns: %TRUE for success, %FALSE if all retry attempts have failed
 */
gboolean
elan_ts_hid_read_hello_packet_bc_version_with_retry(FuDevice *device,
                                                   guint8 *p_hello_packet,
                                                   guint16 *p_bc_version,
                                                   GError **error);

/**
 * elan_ts_hid_read_boot_code_version:
 * @device: a #FuDevice
 * @p_bc_version: (out): stores the parsed 16-bit boot code version
 * @error: (nullable): a #GError, or %NULL
 *
 * Fetches the Boot Code version from the device while in Normal Mode.
 * It sends command 0x53 and expects a response starting with 0x52.
 *
 * Returns: %TRUE for success, %FALSE for failure
 */
gboolean
elan_ts_hid_read_boot_code_version(FuDevice *device, guint16 *p_bc_version, GError **error);

/**
 * elan_ts_hid_read_fw_id:
 * @device: a #FuDevice
 * @p_fw_id: (out): stores the parsed 16-bit firmware ID
 * @error: (nullable): a #GError, or %NULL
 *
 * Fetches the Firmware ID from the device.
 * It sends command {0x53, 0xf0, 0x00, 0x01} and expects 0x52 pattern in response.
 */
gboolean
elan_ts_hid_read_fw_id(FuDevice *device, guint16 *p_fw_id, GError **error);

/**
 * elan_ts_hid_read_fw_version:
 * @device: a #FuDevice
 * @p_fw_version: (out): stores the parsed 16-bit firmware version
 * @error: (nullable): a #GError, or %NULL
 *
 * Fetches the Firmware Version from the device.
 * It sends command {0x53, 0x00, 0x00, 0x01} and expects 0x52 pattern in response.
 */
gboolean
elan_ts_hid_read_fw_version(FuDevice *device, guint16 *p_fw_version, GError **error);

/**
 * elan_ts_hid_read_test_solution_version:
 * @device: a #FuDevice
 * @p_test_solution_version: (out): stores the parsed 16-bit test-solution version
 * @error: (nullable): a #GError, or %NULL
 *
 * Fetches the Test-Solution version from the device.
 * It sends command {0x53, 0xe0, 0x00, 0x01} and expects 0x52/0xE pattern.
 */
gboolean
elan_ts_hid_read_test_solution_version(FuDevice *device,
                                       guint16 *p_test_solution_version,
                                       GError **error);

/**
 * elan_ts_hid_set_test_mode:
 * @device: a #FuDevice
 * @enabled: %TRUE to enter test mode, %FALSE to exit
 * @error: (nullable): a #GError, or %NULL
 *
 * Switches the ELAN touchscreen device between normal mode and test mode.
 *
 * Returns: %TRUE for success, %FALSE for failure
 */
gboolean
elan_ts_hid_set_test_mode(FuDevice *device, gboolean enabled, GError **error);

/**
 * elan_ts_hid_read_remark_id:
 * @device: a #FuDevice
 * @touch_state: current device state (Normal or Recovery)
 * @fw_version: firmware version (used for IC generation identification)
 * @bc_version: boot code version (used for IC generation identification)
 * @p_remark_id: (out): pointer to store the 2-byte Remark ID
 * @error: (nullable): a #GError, or %NULL
 *
 * Reads the Remark ID from the device's ROM at the predefined address (0x801F).
 *
 * The Remark ID is essential for identifying hardware sub-models and ensuring 
 * that the firmware image is fully compatible with the physical touch controller.
 *
 * Returns: %TRUE if the Remark ID was successfully read, %FALSE otherwise.
 */
gboolean
elan_ts_hid_read_remark_id(FuDevice *device,
			   FuElanTsState touch_state,
			   guint16 fw_version,
			   guint16 bc_version,
			   guint16 *p_remark_id,
			   GError **error);

/**
 * elan_ts_hid_read_info_page_with_retry:
 * @self: a #FuDevice
 * @p_info_page_buf: (out): Buffer to store the information page (must be 128 bytes)
 * @info_page_buf_size: Size of the buffer
 * @error: (nullable): a #GError, or %NULL
 *
 * High-level function to retrieve the Info Page with an automatic retry mechanism.
 *
 * Returns: %TRUE for success, %FALSE for failure
 */
gboolean
elan_ts_hid_read_info_page_with_retry(FuDevice *self,
				      guint8 *p_info_page_buf,
				      gsize info_page_buf_size,
				      GError **error);

/**
 * elan_ts_hid_unlock_flash:
 * @self: a #FuDevice
 * @error: (nullable): a #GError, or %NULL
 *
 * Sends the Write Flash Key command to unlock the flash memory for writing.
 *
 * Returns: %TRUE for success, %FALSE for failure.
 **/
gboolean
elan_ts_hid_unlock_flash(FuDevice *self, GError **error);

/**
 * elan_ts_hid_enter_iap_mode:
 * @self: a #FuDevice
 * @error: (nullable): a #GError, or %NULL
 *
 * Sends the command to trigger the device to enter IAP (Bootloader) mode.
 *
 * Returns: %TRUE for success, %FALSE for failure.
 **/
gboolean
elan_ts_hid_enter_iap_mode(FuDevice *self, GError **error);

/**
 * elan_ts_hid_check_slave_address:
 * @self: a #FuDevice
 * @error: (nullable): a #GError, or %NULL
 *
 * Verifies Bootloader readiness by performing a slave address handshake.
 *
 * Returns: %TRUE for success, %FALSE for failure.
 **/
gboolean
elan_ts_hid_check_slave_address(FuDevice *self, GError **error);

/**
 * elan_ts_hid_write_frame_data:
 * @device: a #FuDevice
 * @data_offset: word-aligned offset for the current frame
 * @data_len: length of valid data in @p_frame_buf
 * @p_frame_buf: raw firmware data for this frame
 * @error: (nullable): a #GError, or %NULL
 *
 * Wraps IAP frame data with the HID Output Report ID (0x03) and 
 * the IAP protocol header (0x21), then sends a fixed-length report.
 *
 * Returns: %TRUE for success, %FALSE for failure
 */
gboolean
elan_ts_hid_write_frame_data(FuDevice *device,
                            guint16 data_offset,
                            gsize data_len,
                            const guint8 *p_frame_buf,
                            GError **error);

/**
 * elan_ts_hid_send_flash_write_command:
 * @device: a #FuDevice
 * @error: (nullable): a #GError, or %NULL
 *
 * Sends the vendor-specific command (0x22) to trigger the flash 
 * burning process on the touch controller.
 *
 * Returns: %TRUE for success, %FALSE for failure
 */
gboolean
elan_ts_hid_send_flash_write_command(FuDevice *device, GError **error);

/**
 * elan_ts_hid_read_flash_write_response:
 * @device: a #FuDevice
 * @p_response: (out): stores the combined 16-bit response value
 * @error: (nullable): a #GError, or %NULL
 *
 * Reads the flash write response from the device and validates the pattern.
 * Expected pattern is 0xAA 0xAA.
 *
 * Returns: %TRUE for success, %FALSE for failure or pattern mismatch.
 */
gboolean
elan_ts_hid_read_flash_write_response(FuDevice *device, guint16 *p_response, GError **error);

/**
 * fu_elan_ts_hid_device_recalibrate_with_retry:
 * @device: a #FuDevice
 * @error: (nullable): a #GError, or %NULL
 *
 * Triggers the hardware re-calibration process with an automatic retry mechanism
 * using the fwupd retry framework.
 *
 * Returns: %TRUE for success, %FALSE if all retry attempts have failed.
 **/
gboolean
fu_elan_ts_hid_device_recalibrate_with_retry(FuDevice *device, GError **error);

