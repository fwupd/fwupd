/*
 * Copyright 2026 Elan Microelectronics Corporation <paul.liang@emc.com.tw>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#pragma once

#include <fwupdplugin.h>
#include <libfwupdplugin/fu-hid-device.h>

/**
 * elan_ts_iap_read_and_update_info_page:
 * @self: a #FuDevice
 * @solution_id: Solution ID of current touch firmware
 * @p_fw_page_buf: (out): Buffer to store the final 132-byte firmware page
 * @fw_page_buf_size: Size of the buffer (ELAN_TS_FW_PAGE_SIZE)
 * @error: (nullable): a #GError, or %NULL
 *
 * Retrieves the current Information Page, updates its flash counter and 
 * timestamp, and prepares the final 132-byte firmware page for updating.
 *
 * Returns: %TRUE for success, %FALSE for failure
 */
gboolean
elan_ts_iap_read_and_update_info_page(FuDevice *self,
				     guint8 solution_id,
				     guint8 *p_fw_page_buf,
				     gsize fw_page_buf_size,
				     GError **error);

/**
 * elan_ts_iap_check_remark_id:
 * @self: a #FuDevice
 * @firmware: a #FuFirmware representing the image to be flashed
 * @touch_state: current device state (Normal or Recovery)
 * @fw_version: current firmware version on the device
 * @bc_version: current boot code version on the device
 * @error: (nullable): a #GError, or %NULL
 *
 * Validates the Remark ID of the hardware against the Remark ID in the firmware.
 *
 * This check ensures that the firmware is compatible with the specific hardware
 * sub-model. If the device has no specific Remark ID (identified as 0xFFFF),
 * the check is bypassed to allow the update.
 *
 * Returns: %TRUE if the Remark IDs match or the IC has no Remark ID; %FALSE otherwise.
 */
gboolean
elan_ts_iap_check_remark_id(FuDevice *self,
			   FuFirmware *firmware,
			   FuElanTsState touch_state,
			   guint16 fw_version,
			   guint16 bc_version,
			   GError **error);

/**
 * elan_ts_iap_switch_to_boot_code:
 * @self: a #FuDevice
 * @recovery: %TRUE if the device is in recovery/corrupted state
 * @error: (nullable): a #GError, or %NULL
 *
 * Transitions the device from Application mode to Bootloader (IAP) mode using HID I/O.
 *
 * Returns: %TRUE for success, %FALSE for failure.
 **/
gboolean
elan_ts_iap_switch_to_boot_code(FuDevice *self, 
                               gboolean recovery, 
                               GError **error);

/**
 * elan_ts_iap_write_firmware_pages:
 * @self: (in): FuDevice instance
 * @p_pages_buf: (in): The raw data buffer (132 bytes to 3960 bytes)
 * @pages_buf_size: (in): Total size of the input buffer
 * @error: (out): Error object
 *
 * High-level function to transfer firmware data and trigger the flash write.
 * It packages data into frames and verifies the hardware status.
 *
 * Returns: %TRUE for success, %FALSE for failure
 */
gboolean
elan_ts_iap_write_firmware_pages(FuDevice *self,
				 const guint8 *p_pages_buf,
				 gsize pages_buf_size,
				 GError **error);

