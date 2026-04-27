/*
 * Copyright 2026 Elan Microelectronics Corporation <paul.liang@emc.com.tw>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-elan-ts-common.h"
#include "fu-elan-ts-iap-api.h"
#include "fu-elan-ts-hidraw-utility.h"
#include "fu-elan-ts-hid-utility.h"
#include "fu-elan-ts-firmware.h"

/*
 * Structure for Firmware Update Timestamp
 * Contains the decoded decimal values for the update date and time.
 */
typedef struct {
	guint16 year;
	guint8 month;
	guint8 day;
	guint8 hour;
	guint8 minute;
} ElanTsFwUpdateTimestamp;

/*
 * Main structure for Firmware Update Information
 * Combines the update counter and the last update timestamp.
 */
typedef struct {
	guint16 counter;
	ElanTsFwUpdateTimestamp timestamp;
} ElanTsFwUpdateInfo;

/**
 * elan_ts_iap_get_debug_setting:
 * @device: a #FuDevice
 *
 * Helper function to retrieve the debug bitmask for IAP operations.
 * This ensures consistency across different hardware interface layers.
 *
 * Returns: the debug setting bitmask.
 **/
static guint32
elan_ts_iap_get_debug_setting(FuDevice *device)
{
	/* Forward the request to the low-level hidraw utility */
	return elan_ts_hidraw_get_debug_setting(device);
}

/**
 * elan_ts_iap_info_page_read_value:
 * @self: (in): FuDevice instance
 * @p_info_page_buf: (in): The 128-byte information page buffer
 * @address: (in): Target memory address within the info page
 * @is_bcd: (in): %TRUE if the value is BCD encoded
 * @order: (in): Byte order (e.g., G_LITTLE_ENDIAN or G_BIG_ENDIAN)
 *
 * Reads a 2-byte word from the info page buffer and converts it if needed.
 *
 * Returns: The decoded decimal or raw value.
 */
static guint16
elan_ts_iap_info_page_read_value(FuDevice *self,
				const guint8 *p_info_page_buf,
				guint16 address,
				gboolean is_bcd,
				guint order)
{
	guint16 offset = 0;
	guint16 value = 0;
	guint16 result = 0;
	guint32 debug_setting = elan_ts_iap_get_debug_setting(self);

	offset = (address - ELAN_TS_MEM_INFO_PAGE_1_ADDR) * 2;

	/* Read 2-byte word from buffer */
	value = fu_memread_uint16(p_info_page_buf + offset, order);

	/* [DEBUG] Show the raw word immediately after reading */
	ELAN_TS_DEBUG(debug_setting, "MEM[0x%04x] raw read: 0x%04x (%u)", address, value, value);

	if (is_bcd) {
		/* Check if any nibble is > 9 (not a valid BCD digit) */
		if (((value >> 12) & 0xF) > 9 || ((value >> 8) & 0xF) > 9 || ((value >> 4) & 0xF) > 10 ||
		    (value & 0xF) > 9) {
			ELAN_TS_DEBUG(debug_setting,
				      "MEM[0x%04x] contains non-BCD digits, using raw value",
				      address);
			result = value;
		} else {
			result = ((value >> 12) & 0xF) * 1000 + ((value >> 8) & 0xF) * 100 +
				 ((value >> 4) & 0xF) * 10 + (value & 0xF);
		}
		/* [DEBUG] Show result after BCD conversion */
		ELAN_TS_DEBUG(debug_setting, "MEM[0x%04x] BCD decoded: %u", address, result);
	} else {
		result = value;
	}

	return result;
}

/**
 * elan_ts_iap_info_page_write_value:
 * @self: (in): FuDevice instance
 * @p_info_page_buf: (out): The 128-byte information page buffer to modify
 * @address: (in): Target memory address within the info page
 * @val: (in): Decimal value to encode and write
 * @is_bcd: (in): %TRUE if the value should be BCD encoded (e.g., Year/Month/Day)
 * @order: (in): Byte order (e.g., %G_LITTLE_ENDIAN)
 *
 * Encodes a decimal value (optionally to BCD) and writes it as a 2-byte word
 * into the information page buffer using the specified byte order.
 */
static void
elan_ts_iap_info_page_write_value(FuDevice *self,
				 guint8 *p_info_page_buf,
				 guint16 address,
				 guint16 val,
				 gboolean is_bcd,
				 guint order)
{
	guint16 offset = 0;
	guint16 val_encoded = val;
	guint32 debug_setting = elan_ts_iap_get_debug_setting(self);

	/* Calculate byte offset: (Address - Base) * 2 */
	offset = (address - ELAN_TS_MEM_INFO_PAGE_1_ADDR) * 2;

	/* Convert Decimal to BCD (Visual Hex) if required */
	if (is_bcd) {
		val_encoded = ((val / 1000) << 12) | (((val / 100) % 10) << 8) |
			      (((val / 10) % 10) << 4) | (val % 10);
	}

	/* Log the write operation for debugging */
	ELAN_TS_DEBUG(debug_setting,
		      "Write Info Page [Addr: 0x%04x]: Val=%u, Encoded=0x%04x (BCD:%s, Order:%u)",
		      address,
		      val,
		      val_encoded,
		      is_bcd ? "yes" : "no",
		      order);

	/* Write 2-byte word to buffer */
	fu_memwrite_uint16(p_info_page_buf + offset, val_encoded, order);
}

/**
 * elan_ts_iap_info_page_read_byte:
 * @self: (in): FuDevice instance
 * @p_info_page_buf: (in): The 128-byte information page buffer
 * @address: (in): Target memory address (Word-aligned address)
 * @is_low_byte: (in): %TRUE to read low byte, %FALSE for high byte of the word
 * @is_bcd: (in): %TRUE to decode from BCD to decimal
 *
 * Reads a single byte from the info page buffer.
 *
 * Returns: The decoded decimal or raw byte value.
 */
static guint8
elan_ts_iap_info_page_read_byte(FuDevice *self,
			       const guint8 *p_info_page_buf,
			       guint16 address,
			       gboolean is_low_byte,
			       gboolean is_bcd)
{
	guint16 offset = (address - ELAN_TS_MEM_INFO_PAGE_1_ADDR) * 2;
	guint8 value = 0;
	guint8 result = 0;
	guint32 debug_setting = elan_ts_iap_get_debug_setting(self);

	/* Identify target byte based on word-addressing logic */
	value = is_low_byte ? p_info_page_buf[offset + 1] : p_info_page_buf[offset];

	if (is_bcd) {
		result = ((value >> 4) & 0xF) * 10 + (value & 0xF);
	} else {
		result = value;
	}

	ELAN_TS_DEBUG(debug_setting,
		      "Read Info Byte [Addr: 0x%04x %s]: Raw=0x%02x, Decoded=%u",
		      address,
		      is_low_byte ? "Low" : "High",
		      value,
		      result);

	return result;
}

/**
 * elan_ts_iap_info_page_write_byte:
 * @self: (in): FuDevice instance
 * @p_info_page_buf: (out): The 128-byte info page buffer to modify
 * @address: (in): Target memory address (Word-aligned address)
 * @is_low_byte: (in): %TRUE to write low byte, %FALSE for high byte
 * @value: (in): Decimal value to write
 * @is_bcd: (in): %TRUE to encode as BCD
 */
static void
elan_ts_iap_info_page_write_byte(FuDevice *self,
				guint8 *p_info_page_buf,
				guint16 address,
				gboolean is_low_byte,
				guint8 value,
				gboolean is_bcd)
{
	guint16 offset = (address - ELAN_TS_MEM_INFO_PAGE_1_ADDR) * 2;
	guint8 value_encoded = 0;
	guint32 debug_setting = elan_ts_iap_get_debug_setting(self);

	if (is_bcd) {
		value_encoded = ((value / 10) << 4) | (value % 10);
	} else {
		value_encoded = value;
	}

	if (is_low_byte)
		p_info_page_buf[offset + 1] = value_encoded;
	else
		p_info_page_buf[offset] = value_encoded;

	ELAN_TS_DEBUG(debug_setting,
		      "Write Info Byte [Addr: 0x%04x %s]: Val=%u, Encoded=0x%02x",
		      address,
		      is_low_byte ? "Low" : "High",
		      value,
		      value_encoded);
}

/**
 * elan_ts_iap_fill_fw_page:
 * @self: (in): FuDevice instance
 * @mem_page_address: (in): Target memory address (e.g., 0x0040 or 0x8040)
 * @p_fw_page_data_buf: (in): The 128-byte raw data to be packaged
 * @fw_page_data_buf_size: (in): Size of the input data buffer (ELAN_TS_FW_PAGE_DATA_SIZE)
 * @p_fw_page_buf: (out): Buffer to store the final 132-byte firmware page
 * @fw_page_buf_size: (in): Size of the output buffer (ELAN_TS_FW_PAGE_SIZE)
 * @error: (out): Error object
 *
 * Packages raw data with an address header and a calculated checksum.
 * Structure: 2-byte Addr + 128-byte Data + 2-byte Checksum = 132 bytes.
 *
 * Returns: %TRUE for success, %FALSE for failure
 */
static gboolean
elan_ts_iap_fill_fw_page(FuDevice *self,
                        guint16 mem_page_address,
                        const guint8 *p_fw_page_data_buf,
                        gsize fw_page_data_buf_size,
                        guint8 *p_fw_page_buf,
                        gsize fw_page_buf_size,
                        GError **error)
{
	guint16 page_checksum = 0;
	guint16 page_data = 0;
	guint32 debug_setting = elan_ts_iap_get_debug_setting(self);

	/* Validate Arguments */
	g_return_val_if_fail(p_fw_page_data_buf != NULL, FALSE);
	g_return_val_if_fail(p_fw_page_buf != NULL, FALSE);

	if (fw_page_buf_size < ELAN_TS_FW_PAGE_SIZE || fw_page_data_buf_size < ELAN_TS_FW_PAGE_DATA_SIZE) {
		ELAN_TS_SET_ERROR(debug_setting,
				  error,
				  FWUPD_ERROR_INVALID_DATA,
				  "invalid buffer sizes (output: %" G_GSIZE_FORMAT ", data: %" G_GSIZE_FORMAT ")",
				  fw_page_buf_size,
				  fw_page_data_buf_size);
		return FALSE;
	}

	/* Set Page Address (Header) at index 0, 1 - Little-Endian */
	p_fw_page_buf[0] = (guint8)(mem_page_address & 0xFF);
	p_fw_page_buf[1] = (guint8)((mem_page_address >> 8) & 0xFF);

	/* Set Page Data (128 bytes) starting from index 2 */
	memcpy(&p_fw_page_buf[2], p_fw_page_data_buf, ELAN_TS_FW_PAGE_DATA_SIZE);

	/* Compute Page Checksum */
	/* Iterate through Address and Data (first 130 bytes) */
	for (guint i = 0; i < (ELAN_TS_FW_PAGE_SIZE - 2); i += 2) {
		/* Get Page Data in Little-Endian */
		page_data = (guint16)(p_fw_page_buf[i + 1] << 8) | p_fw_page_buf[i];

		/* Special logic: If page address is 0x0040, use 0x8040 for checksum calculation */
		if (i == 0 && page_data == ELAN_TS_MEM_INFO_PAGE_WRITE_ADDR) {
			page_data = ELAN_TS_MEM_INFO_PAGE_1_ADDR;
		}
		page_checksum += page_data;
	}

	/* Set Page Checksum (Footer) at the last 2 bytes - Little-Endian */
	p_fw_page_buf[ELAN_TS_FW_PAGE_SIZE - 2] = (guint8)(page_checksum & 0xFF);
	p_fw_page_buf[ELAN_TS_FW_PAGE_SIZE - 1] = (guint8)((page_checksum >> 8) & 0xFF);

	ELAN_TS_DEBUG(debug_setting,
		      "FW Page Setup complete: Addr=0x%04x, Checksum=0x%04x",
		      mem_page_address,
		      page_checksum);

	return TRUE;
}

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
                                     GError **error)
{
	guint8 info_mem_page_buf[ELAN_TS_MEMORY_PAGE_SIZE] = {0};
	ElanTsFwUpdateInfo fw_update_info = {0};
	g_autoptr(GDateTime) p_time_now = g_date_time_new_now_local();
	guint16 memory_page_address = 0;
	guint32 debug_setting = elan_ts_iap_get_debug_setting(self);

	/* Sanity Check */
	g_return_val_if_fail(FU_IS_DEVICE(self), FALSE);
	g_return_val_if_fail(p_fw_page_buf != NULL, FALSE);

	if (fw_page_buf_size < ELAN_TS_FW_PAGE_SIZE) {
		ELAN_TS_SET_ERROR(debug_setting,
				  error,
				  FWUPD_ERROR_INVALID_DATA,
				  "invalid output buffer size: %" G_GSIZE_FORMAT,
				  fw_page_buf_size);
		return FALSE;
	}

	/* Get Info Page Data via updated HID read-with-retry function */
	if (!elan_ts_hid_read_info_page_with_retry(self,
						  info_mem_page_buf,
						  sizeof(info_mem_page_buf),
						  error)) {
		ELAN_TS_ERROR(debug_setting, error, "failed to read information page: ");
		return FALSE;
	}

	/* Parse Current FW Update Information (Read as individual bytes) */
	fw_update_info.counter = elan_ts_iap_info_page_read_value(self,
								 info_mem_page_buf,
								 ELAN_TS_MEM_UPDATE_COUNTER_ADDR,
								 FALSE,
								 G_LITTLE_ENDIAN);

	/* If counter value is 65535 (0xFFFF), reset it to 0 */
	if (fw_update_info.counter == 0xFFFF) {
		ELAN_TS_DEBUG(debug_setting, "counter is 0xFFFF, resetting to 0");
		fw_update_info.counter = 0;
	}

	fw_update_info.timestamp.year =
	    elan_ts_iap_info_page_read_value(self,
					     info_mem_page_buf,
					     ELAN_TS_MEM_LAST_UPDATE_YEAR_ADDR,
					     TRUE,
					     G_LITTLE_ENDIAN);

	/* Month and Day: High Byte is Month, Low Byte is Day */
	fw_update_info.timestamp.month =
	    elan_ts_iap_info_page_read_byte(self,
					    info_mem_page_buf,
					    ELAN_TS_MEM_LAST_UPDATE_MONTH_DAY_ADDR,
					    FALSE,
					    TRUE);
	fw_update_info.timestamp.day =
	    elan_ts_iap_info_page_read_byte(self,
					    info_mem_page_buf,
					    ELAN_TS_MEM_LAST_UPDATE_MONTH_DAY_ADDR,
					    TRUE,
					    TRUE);

	/* Hour and Minute: High Byte is Hour, Low Byte is Minute */
	fw_update_info.timestamp.hour =
	    elan_ts_iap_info_page_read_byte(self,
					    info_mem_page_buf,
					    ELAN_TS_MEM_LAST_UPDATE_TIME_ADDR,
					    FALSE,
					    TRUE);
	fw_update_info.timestamp.minute =
	    elan_ts_iap_info_page_read_byte(self,
					    info_mem_page_buf,
					    ELAN_TS_MEM_LAST_UPDATE_TIME_ADDR,
					    TRUE,
					    TRUE);

	/* Refresh FW Update Info. (Counter++ and Update Timestamp) */
	fw_update_info.counter++;
	fw_update_info.timestamp.year = (guint16)g_date_time_get_year(p_time_now);
	fw_update_info.timestamp.month = (guint8)g_date_time_get_month(p_time_now);
	fw_update_info.timestamp.day = (guint8)g_date_time_get_day_of_month(p_time_now);
	fw_update_info.timestamp.hour = (guint8)g_date_time_get_hour(p_time_now);
	fw_update_info.timestamp.minute = (guint8)g_date_time_get_minute(p_time_now);

	/* Update Buffer Data (Write back as individual bytes) */
	elan_ts_iap_info_page_write_value(self,
					 info_mem_page_buf,
					 ELAN_TS_MEM_UPDATE_COUNTER_ADDR,
					 fw_update_info.counter,
					 FALSE,
					 G_LITTLE_ENDIAN);
	elan_ts_iap_info_page_write_value(self,
					 info_mem_page_buf,
					 ELAN_TS_MEM_LAST_UPDATE_YEAR_ADDR,
					 fw_update_info.timestamp.year,
					 TRUE,
					 G_LITTLE_ENDIAN);

	/* Month and Day */
	elan_ts_iap_info_page_write_byte(self,
					info_mem_page_buf,
					ELAN_TS_MEM_LAST_UPDATE_MONTH_DAY_ADDR,
					FALSE,
					fw_update_info.timestamp.month,
					TRUE);
	elan_ts_iap_info_page_write_byte(self,
					info_mem_page_buf,
					ELAN_TS_MEM_LAST_UPDATE_MONTH_DAY_ADDR,
					TRUE,
					fw_update_info.timestamp.day,
					TRUE);

	/* Hour and Minute */
	elan_ts_iap_info_page_write_byte(self,
					info_mem_page_buf,
					ELAN_TS_MEM_LAST_UPDATE_TIME_ADDR,
					FALSE,
					fw_update_info.timestamp.hour,
					TRUE);
	elan_ts_iap_info_page_write_byte(self,
					info_mem_page_buf,
					ELAN_TS_MEM_LAST_UPDATE_TIME_ADDR,
					TRUE,
					fw_update_info.timestamp.minute,
					TRUE);

	/* Determine Write Address based on Solution ID */
	if (solution_id == ELAN_TS_HID_SOLUTION_ID_EKTH6315x1 ||
	    solution_id == ELAN_TS_HID_SOLUTION_ID_EKTH6315x2 ||
	    solution_id == ELAN_TS_HID_SOLUTION_ID_EKTH6315_TO_5015M ||
	    solution_id == ELAN_TS_HID_SOLUTION_ID_EKTH6315_TO_3915P ||
	    solution_id == ELAN_TS_HID_SOLUTION_ID_EKTH6308x1 ||
	    solution_id == ELAN_TS_HID_SOLUTION_ID_EKTH7315x1 ||
	    solution_id == ELAN_TS_HID_SOLUTION_ID_EKTH7315x2 ||
	    solution_id == ELAN_TS_HID_SOLUTION_ID_EKTH7318x1) {
		memory_page_address = ELAN_TS_MEM_INFO_PAGE_WRITE_ADDR; /* 0x0040 */
	} else {
		memory_page_address = ELAN_TS_MEM_INFO_PAGE_1_ADDR; /* 0x8040 */
	}

	/* Fill Final Page (132 bytes with Checksum) */
	if (!elan_ts_iap_fill_fw_page(self,
				     memory_page_address,
				     info_mem_page_buf,
				     sizeof(info_mem_page_buf),
				     p_fw_page_buf,
				     fw_page_buf_size,
				     error)) {
		ELAN_TS_ERROR(debug_setting, error, "failed to fill firmware page: ");
		return FALSE;
	}

	return TRUE;
}

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
			   GError **error)
{
	guint32 debug_setting = elan_ts_iap_get_debug_setting(self);
	guint16 remark_id_from_rom = 0;
	guint16 remark_id_from_fw = 0;

	/* Basic sanity check */
	g_return_val_if_fail(FU_IS_DEVICE(self), FALSE);
	g_return_val_if_fail(FU_IS_FIRMWARE(firmware), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* Get Remark ID from hardware ROM - This is a MANDATORY prerequisite */
	if (!elan_ts_hid_read_remark_id(self,
				       touch_state,
				       fw_version,
				       bc_version,
				       &remark_id_from_rom,
				       error)) {
		ELAN_TS_ERROR(debug_setting, error, "failed to get Remark ID from ROM: ");
		return FALSE;
	}

	/* Read Remark ID from Firmware Image
	 * Note: Using FuFirmware directly and assuming the underlying object
	 * is of the correct ELAN TS firmware type. */
	remark_id_from_fw = elan_ts_firmware_get_remark_id(FU_ELAN_TS_FIRMWARE(firmware));

	ELAN_TS_DEBUG(debug_setting,
		      "Remark ID from ROM: 0x%04x, Remark ID from FW: 0x%04x",
		      remark_id_from_rom,
		      remark_id_from_fw);

	/* Validate Remark ID Logic */
	if (remark_id_from_rom == ELAN_TS_REMARK_ID_NONE) {
		/* Non-Remark IC (0xFFFF): Bypass the verification */
		ELAN_TS_DEBUG(debug_setting, "Non-Remark IC (0x%04x), bypassing check.", remark_id_from_rom);
		return TRUE;
	}

	/* Strict match required for Remark ICs */
	if (remark_id_from_rom != remark_id_from_fw) {
		ELAN_TS_SET_ERROR(debug_setting,
				  error,
				  FWUPD_ERROR_INVALID_DATA,
				  "Remark ID mismatched! (ROM: 0x%04x, FW: 0x%04x)",
				  remark_id_from_rom,
				  remark_id_from_fw);
		return FALSE;
	}

	ELAN_TS_DEBUG(debug_setting, "Remark ID validation successful.");
	return TRUE;
}

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
elan_ts_iap_switch_to_boot_code(FuDevice *self, gboolean recovery, GError **error)
{
	guint32 debug_setting = elan_ts_iap_get_debug_setting(self);

	/* Sanity check */
	g_return_val_if_fail(FU_IS_DEVICE(self), FALSE);

	ELAN_TS_DEBUG(debug_setting, "Switching to Bootloader mode (recovery=%s)", 
	              recovery ? "TRUE" : "FALSE");

	/* Unlock Flash */
	if (!elan_ts_hid_unlock_flash(self, error)) {
		/* Error already logged in elan_ts_hid_unlock_flash */
		return FALSE;
	}
	ELAN_TS_DEBUG(debug_setting, "Flash Key sent successfully");

	/* Trigger IAP Entry (Bypassed in Recovery mode) */
	if (!recovery) {
		if (!elan_ts_hid_enter_iap_mode(self, error)) {
			/* Error already logged in elan_ts_hid_enter_iap_mode */
			return FALSE;
		}
		ELAN_TS_DEBUG(debug_setting, "Enter IAP command sent successfully");
	} else {
		ELAN_TS_DEBUG(debug_setting, "Device in recovery mode: skipping Enter IAP command");
	}

	/* Wait for hardware re-initialization (15ms) */
	g_usleep(15 * 1000);

	/* Verify Bootloader responsiveness */
	if (!elan_ts_hid_check_slave_address(self, error)) {
		ELAN_TS_SET_ERROR(debug_setting,
				  error,
				  FWUPD_ERROR_WRITE,
				  "device failed to respond in Bootloader mode after 15ms");
		return FALSE;
	}

	ELAN_TS_DEBUG(debug_setting, "Successfully switched to Bootloader mode");
	return TRUE;
}

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
				 GError **error)
{
	guint32 debug_setting = elan_ts_iap_get_debug_setting(self);
	gsize offset = 0;
	gsize data_len = 0;
	gsize frame_count = 0;
	guint frame_index = 0;
	guint16 flash_write_response = 0;
	gboolean is_multi_page = (pages_buf_size > ELAN_TS_FW_PAGE_SIZE);

	/* Sanity check for input arguments */
	g_return_val_if_fail(FU_IS_DEVICE(self), FALSE);
	g_return_val_if_fail(p_pages_buf != NULL, FALSE);

	/* Validate buffer size limits (Max 30 pages) */
	if (pages_buf_size == 0 || pages_buf_size > (ELAN_TS_FW_PAGE_SIZE * 30)) {
		ELAN_TS_SET_ERROR(debug_setting,
				  error,
				  FWUPD_ERROR_INVALID_DATA,
				  "Invalid IAP buffer size: %" G_GSIZE_FORMAT,
				  pages_buf_size);
		return FALSE;
	}

	/* Calculate total frames required based on protocol limits */
	frame_count = (pages_buf_size + ELAN_TS_HID_PAGE_FRAME_SIZE - 1) / ELAN_TS_HID_PAGE_FRAME_SIZE;

	/* Transfer data frames sequentially */
	for (frame_index = 0; frame_index < frame_count; frame_index++) {
		offset = frame_index * ELAN_TS_HID_PAGE_FRAME_SIZE;
		data_len = MIN(pages_buf_size - offset, ELAN_TS_HID_PAGE_FRAME_SIZE);

		if (!elan_ts_hid_write_frame_data(self,
						  (guint16)offset,
						  data_len,
						  p_pages_buf + offset,
						  error)) {
			ELAN_TS_ERROR(debug_setting, error, "Failed to send data frame %u: ", frame_index);
			return FALSE;
		}
	}

	/* Request hardware to execute flash write operation */
	if (!elan_ts_hid_send_flash_write_command(self, error)) {
		return FALSE;
	}

	/* Sleep to allow flash burning completion */
	if (is_multi_page)
		fu_device_sleep(self, 360); /* Long wait for multi-page block */
	else
		fu_device_sleep(self, 15);  /* Short wait for single page (e.g. Info Page) */

	/* Read back and verify the execution response */
	if (!elan_ts_hid_read_flash_write_response(self, &flash_write_response, error)) {
		return FALSE;
	}

	ELAN_TS_DEBUG(debug_setting,
		      "Update firmware pages successful: %" G_GSIZE_FORMAT " bytes written",
		      pages_buf_size);

	return TRUE;
}

