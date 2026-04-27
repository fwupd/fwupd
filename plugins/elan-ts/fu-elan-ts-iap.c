/*
 * Copyright 2026 Elan Microelectronics Corporation <paul.liang@emc.com.tw>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-elan-ts-common.h"
#include "fu-elan-ts-firmware.h"
#include "fu-elan-ts-hid.h"
#include "fu-elan-ts-hidraw.h"
#include "fu-elan-ts-iap.h"

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
} FuElanTsFwUpdateTimestamp;

/*
 * Main structure for Firmware Update Information
 * Combines the update counter and the last update timestamp.
 */
typedef struct {
	guint16 counter;
	FuElanTsFwUpdateTimestamp timestamp;
} FuElanTsFwUpdateInfo;

/**
 * fu_elan_ts_iap_info_page_read_value:
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
fu_elan_ts_iap_info_page_read_value(const guint8 *p_info_page_buf,
				    guint16 address,
				    gboolean is_bcd,
				    guint order)
{
	guint16 offset = 0;
	guint16 value = 0;
	guint16 result = 0;

	offset = (address - ELAN_TS_MEM_INFO_PAGE_1_ADDR) * 2;

	/* read 2-byte word from buffer */
	value = fu_memread_uint16(p_info_page_buf + offset, order);
	g_debug("mem[0x%04x] raw read: 0x%04x (%u)", address, value, value);

	if (is_bcd) {
		/* check if any nibble is > 9 (not a valid BCD digit) */
		if ((((value >> 12) & 0xF) > 9) || (((value >> 8) & 0xF) > 9) ||
		    (((value >> 4) & 0xF) > 9) || ((value & 0xF) > 9)) {
			g_debug("mem[0x%04x] contains non-BCD digits, using raw value", address);
			result = value;
		} else {
			result = ((value >> 12) & 0xF) * 1000 + ((value >> 8) & 0xF) * 100 +
				 ((value >> 4) & 0xF) * 10 + (value & 0xF);
		}
		g_debug("mem[0x%04x] BCD decoded: %u", address, result);
	} else {
		result = value;
	}

	return result;
}

/**
 * fu_elan_ts_iap_info_page_write_value:
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
fu_elan_ts_iap_info_page_write_value(guint8 *p_info_page_buf,
				     guint16 address,
				     guint16 val,
				     gboolean is_bcd,
				     guint order)
{
	guint16 offset = 0;
	guint16 val_encoded = val;

	/* calculate byte offset: (Address - Base) * 2 */
	offset = (address - ELAN_TS_MEM_INFO_PAGE_1_ADDR) * 2;

	/* convert Decimal to BCD (Visual Hex) if required */
	if (is_bcd) {
		val_encoded = ((val / 1000) << 12) | (((val / 100) % 10) << 8) |
			      (((val / 10) % 10) << 4) | (val % 10);
	}

	g_debug("write Info Page [Addr: 0x%04x]: Val=%u, Encoded=0x%04x (BCD:%s, Order:%u)",
		address,
		val,
		val_encoded,
		is_bcd ? "yes" : "no",
		order);

	/* write 2-byte word to buffer */
	fu_memwrite_uint16(p_info_page_buf + offset, val_encoded, order);
}

/**
 * fu_elan_ts_iap_info_page_read_byte:
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
fu_elan_ts_iap_info_page_read_byte(const guint8 *p_info_page_buf,
				   guint16 address,
				   gboolean is_low_byte,
				   gboolean is_bcd)
{
	guint16 offset = (address - ELAN_TS_MEM_INFO_PAGE_1_ADDR) * 2;
	guint8 value = 0;
	guint8 result = 0;

	/* identify target byte based on word-addressing logic */
	value = is_low_byte ? p_info_page_buf[offset + 1] : p_info_page_buf[offset];

	if (is_bcd)
		result = ((value >> 4) & 0xF) * 10 + (value & 0xF);
	else
		result = value;

	g_debug("read Info Byte [Addr: 0x%04x %s]: Raw=0x%02x, Decoded=%u",
		address,
		is_low_byte ? "Low" : "High",
		value,
		result);

	return result;
}

/**
 * fu_elan_ts_iap_info_page_write_byte:
 * @p_info_page_buf: (out): The 128-byte info page buffer to modify
 * @address: (in): Target memory address (Word-aligned address)
 * @is_low_byte: (in): %TRUE to write low byte, %FALSE for high byte
 * @value: (in): Decimal value to write
 * @is_bcd: (in): %TRUE to encode as BCD
 */
static void
fu_elan_ts_iap_info_page_write_byte(guint8 *p_info_page_buf,
				    guint16 address,
				    gboolean is_low_byte,
				    guint8 value,
				    gboolean is_bcd)
{
	guint16 offset = (address - ELAN_TS_MEM_INFO_PAGE_1_ADDR) * 2;
	guint8 value_encoded = 0;

	if (is_bcd)
		value_encoded = ((value / 10) << 4) | (value % 10);
	else
		value_encoded = value;

	if (is_low_byte)
		p_info_page_buf[offset + 1] = value_encoded;
	else
		p_info_page_buf[offset] = value_encoded;

	g_debug("write Info Byte [Addr: 0x%04x %s]: Val=%u, Encoded=0x%02x",
		address,
		is_low_byte ? "Low" : "High",
		value,
		value_encoded);
}

/**
 * fu_elan_ts_iap_fill_fw_page:
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
fu_elan_ts_iap_fill_fw_page(guint16 mem_page_address,
			    const guint8 *p_fw_page_data_buf,
			    gsize fw_page_data_buf_size,
			    guint8 *p_fw_page_buf,
			    gsize fw_page_buf_size,
			    GError **error)
{
	guint16 page_checksum = 0;
	guint16 page_data = 0;
	guint index = 0;

	/* basic sanity checks */
	g_return_val_if_fail(p_fw_page_data_buf != NULL, FALSE);
	g_return_val_if_fail(p_fw_page_buf != NULL, FALSE);
	g_return_val_if_fail((error == NULL) || (*error == NULL), FALSE);

	if ((fw_page_buf_size < ELAN_TS_FW_PAGE_SIZE) ||
	    (fw_page_data_buf_size < ELAN_TS_FW_PAGE_DATA_SIZE)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "invalid buffer sizes (output: %" G_GSIZE_FORMAT
			    ", data: %" G_GSIZE_FORMAT ")",
			    fw_page_buf_size,
			    fw_page_data_buf_size);
		return FALSE;
	}

	/* set Page Address (Header) at index 0, 1 - Little-Endian */
	p_fw_page_buf[0] = (guint8)(mem_page_address & 0xFF);
	p_fw_page_buf[1] = (guint8)((mem_page_address >> 8) & 0xFF);

	/* set Page Data (128 bytes) starting from index 2 */
	if (!fu_memcpy_safe(p_fw_page_buf,	       /* dst */
			    fw_page_buf_size,	       /* dst_sz */
			    2,			       /* dst_offset */
			    p_fw_page_data_buf,	       /* src */
			    fw_page_data_buf_size,     /* src_sz */
			    0,			       /* src_offset */
			    ELAN_TS_FW_PAGE_DATA_SIZE, /* n */
			    error)) {
		g_prefix_error_literal(error, "failed to copy page data: ");
		return FALSE;
	}

	/* compute page checksum */

	/* iterate through Address and Data (first 130 bytes) */
	for (index = 0; index < (ELAN_TS_FW_PAGE_SIZE - 2); index += 2) {
		/* get Page Data in Little-Endian */
		page_data = (guint16)(p_fw_page_buf[index + 1] << 8) | p_fw_page_buf[index];

		/* if page address is 0x0040, use 0x8040 for checksum calculation */
		if ((index == 0) && (page_data == ELAN_TS_MEM_INFO_PAGE_WRITE_ADDR))
			page_data = ELAN_TS_MEM_INFO_PAGE_1_ADDR;
		page_checksum += page_data;
	}

	/* set Page Checksum (Footer) at the last 2 bytes - Little-Endian */
	p_fw_page_buf[ELAN_TS_FW_PAGE_SIZE - 2] = (guint8)(page_checksum & 0xFF);
	p_fw_page_buf[ELAN_TS_FW_PAGE_SIZE - 1] = (guint8)((page_checksum >> 8) & 0xFF);
	g_debug("fw page setup complete: Addr=0x%04x, Checksum=0x%04x",
		mem_page_address,
		page_checksum);

	return TRUE;
}

/**
 * fu_elan_ts_iap_read_and_update_info_page:
 * @device: a #FuHidrawDevice
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
fu_elan_ts_iap_read_and_update_info_page(FuHidrawDevice *device,
					 guint8 solution_id,
					 guint8 *p_fw_page_buf,
					 gsize fw_page_buf_size,
					 GError **error)
{
	FuElanTsFwUpdateInfo fw_update_info = {0};
	g_autoptr(GDateTime) p_time_now = g_date_time_new_now_local();
	guint8 info_mem_page_buf[ELAN_TS_MEMORY_PAGE_SIZE] = {0};
	guint16 memory_page_address = 0;

	/* basic sanity checks */
	g_return_val_if_fail(FU_IS_HIDRAW_DEVICE(device), FALSE);
	g_return_val_if_fail(p_fw_page_buf != NULL, FALSE);
	g_return_val_if_fail((error == NULL) || (*error == NULL), FALSE);

	/* ensure local system time was successfully acquired to prevent null dereference */
	if (p_time_now == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "failed to get current local time");
		return FALSE;
	}

	/* validate the provided firmware page buffer size */
	if (fw_page_buf_size < ELAN_TS_FW_PAGE_SIZE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "invalid output buffer size: %" G_GSIZE_FORMAT,
			    fw_page_buf_size);
		return FALSE;
	}

	/* get Info Page Data via updated HID read-with-retry function */
	if (!fu_elan_ts_hid_read_info_page_with_retry(device,
						      info_mem_page_buf,
						      sizeof(info_mem_page_buf),
						      error)) {
		g_prefix_error_literal(error, "failed to read Information Page: ");
		return FALSE;
	}

	/* parse Current FW Update Information (Read as individual bytes) */
	fw_update_info.counter =
	    fu_elan_ts_iap_info_page_read_value(info_mem_page_buf,
						ELAN_TS_MEM_UPDATE_COUNTER_ADDR,
						FALSE,
						G_LITTLE_ENDIAN);

	/* if counter value is 65535 (0xFFFF), reset it to 0 */
	if (fw_update_info.counter == 0xFFFF) {
		g_debug("counter is 0xFFFF, resetting to 0");
		fw_update_info.counter = 0;
	}

	fw_update_info.timestamp.year =
	    fu_elan_ts_iap_info_page_read_value(info_mem_page_buf,
						ELAN_TS_MEM_LAST_UPDATE_YEAR_ADDR,
						TRUE,
						G_LITTLE_ENDIAN);

	/* month & day: High Byte is Month, Low Byte is Day */
	fw_update_info.timestamp.month =
	    fu_elan_ts_iap_info_page_read_byte(info_mem_page_buf,
					       ELAN_TS_MEM_LAST_UPDATE_MONTH_DAY_ADDR,
					       FALSE,
					       TRUE);
	fw_update_info.timestamp.day =
	    fu_elan_ts_iap_info_page_read_byte(info_mem_page_buf,
					       ELAN_TS_MEM_LAST_UPDATE_MONTH_DAY_ADDR,
					       TRUE,
					       TRUE);

	/* hour & minute: High Byte is Hour, Low Byte is Minute */
	fw_update_info.timestamp.hour =
	    fu_elan_ts_iap_info_page_read_byte(info_mem_page_buf,
					       ELAN_TS_MEM_LAST_UPDATE_TIME_ADDR,
					       FALSE,
					       TRUE);
	fw_update_info.timestamp.minute =
	    fu_elan_ts_iap_info_page_read_byte(info_mem_page_buf,
					       ELAN_TS_MEM_LAST_UPDATE_TIME_ADDR,
					       TRUE,
					       TRUE);

	/* refresh FW Update Info. (Counter++ with overflow clamp, and Update Timestamp) */
	if (fu_device_has_flag(FU_DEVICE(device), FWUPD_DEVICE_FLAG_EMULATED) ||
	    fu_device_has_flag(FU_DEVICE(device), FWUPD_DEVICE_FLAG_EMULATION_TAG)) {
		/* Force a completely fixed timestamp and counter in emulation mode (e.g., matching
		 * the captured JSON time) */
		fw_update_info.counter = 1;
		fw_update_info.timestamp.year = 2026;
		fw_update_info.timestamp.month = 6;
		fw_update_info.timestamp.day = 3;
		fw_update_info.timestamp.hour = 12;
		fw_update_info.timestamp.minute = 0;
	} else {
		/* In hardware mode, read the actual current time and increment the counter */
		if (fw_update_info.counter < 0xFFFE)
			fw_update_info.counter++;
		else
			g_debug("firmware update counter has reached its maximum limit (0xFFFE), "
				"clamping");
		fw_update_info.timestamp.year = (guint16)g_date_time_get_year(p_time_now);
		fw_update_info.timestamp.month = (guint8)g_date_time_get_month(p_time_now);
		fw_update_info.timestamp.day = (guint8)g_date_time_get_day_of_month(p_time_now);
		fw_update_info.timestamp.hour = (guint8)g_date_time_get_hour(p_time_now);
		fw_update_info.timestamp.minute = (guint8)g_date_time_get_minute(p_time_now);
	}

	/* update Buffer Data (Write back as individual bytes) */
	fu_elan_ts_iap_info_page_write_value(info_mem_page_buf,
					     ELAN_TS_MEM_UPDATE_COUNTER_ADDR,
					     fw_update_info.counter,
					     FALSE,
					     G_LITTLE_ENDIAN);
	fu_elan_ts_iap_info_page_write_value(info_mem_page_buf,
					     ELAN_TS_MEM_LAST_UPDATE_YEAR_ADDR,
					     fw_update_info.timestamp.year,
					     TRUE,
					     G_LITTLE_ENDIAN);

	/* month & day */
	fu_elan_ts_iap_info_page_write_byte(info_mem_page_buf,
					    ELAN_TS_MEM_LAST_UPDATE_MONTH_DAY_ADDR,
					    FALSE,
					    fw_update_info.timestamp.month,
					    TRUE);
	fu_elan_ts_iap_info_page_write_byte(info_mem_page_buf,
					    ELAN_TS_MEM_LAST_UPDATE_MONTH_DAY_ADDR,
					    TRUE,
					    fw_update_info.timestamp.day,
					    TRUE);

	/* hour & minute */
	fu_elan_ts_iap_info_page_write_byte(info_mem_page_buf,
					    ELAN_TS_MEM_LAST_UPDATE_TIME_ADDR,
					    FALSE,
					    fw_update_info.timestamp.hour,
					    TRUE);
	fu_elan_ts_iap_info_page_write_byte(info_mem_page_buf,
					    ELAN_TS_MEM_LAST_UPDATE_TIME_ADDR,
					    TRUE,
					    fw_update_info.timestamp.minute,
					    TRUE);

	/* determine Write Address based on Solution ID */
	if ((solution_id == FU_ELAN_TS_SOLUTION_ID_EKTH6315X1) ||
	    (solution_id == FU_ELAN_TS_SOLUTION_ID_EKTH6315X2) ||
	    (solution_id == FU_ELAN_TS_SOLUTION_ID_EKTH6315_TO_5015M) ||
	    (solution_id == FU_ELAN_TS_SOLUTION_ID_EKTH6315_TO_3915P) ||
	    (solution_id == FU_ELAN_TS_SOLUTION_ID_EKTH6308X1) ||
	    (solution_id == FU_ELAN_TS_SOLUTION_ID_EKTH7315X1) ||
	    (solution_id == FU_ELAN_TS_SOLUTION_ID_EKTH7315X2) ||
	    (solution_id == FU_ELAN_TS_SOLUTION_ID_EKTH7318X1))
		memory_page_address = ELAN_TS_MEM_INFO_PAGE_WRITE_ADDR; /* 0x0040 */
	else
		memory_page_address = ELAN_TS_MEM_INFO_PAGE_1_ADDR; /* 0x8040 */

	/* fill Final Page (132 bytes with Checksum) */
	if (!fu_elan_ts_iap_fill_fw_page(memory_page_address,
					 info_mem_page_buf,
					 sizeof(info_mem_page_buf),
					 p_fw_page_buf,
					 fw_page_buf_size,
					 error)) {
		g_prefix_error_literal(error, "failed to fill firmware page: ");
		return FALSE;
	}

	return TRUE;
}

/**
 * fu_elan_ts_iap_check_remark_id:
 * @device: a #FuHidrawDevice
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
fu_elan_ts_iap_check_remark_id(FuHidrawDevice *device,
			       FuFirmware *firmware,
			       FuElanTsState touch_state,
			       guint16 fw_version,
			       guint16 bc_version,
			       GError **error)
{
	guint16 remark_id_from_rom = 0;
	guint16 remark_id_from_fw = 0;

	/* basic sanity checks */
	g_return_val_if_fail(FU_IS_HIDRAW_DEVICE(device), FALSE);
	g_return_val_if_fail(FU_IS_ELAN_TS_FIRMWARE(firmware), FALSE);
	g_return_val_if_fail((error == NULL) || (*error == NULL), FALSE);

	/* get Remark ID from hardware ROM - This is a MANDATORY prerequisite */
	if (!fu_elan_ts_hid_read_remark_id(device,
					   touch_state,
					   fw_version,
					   bc_version,
					   &remark_id_from_rom,
					   error)) {
		g_prefix_error_literal(error, "failed to get Remark ID from ROM: ");
		return FALSE;
	}

	/* Read Remark ID from Firmware Image
	 * Note: Using FuFirmware directly and assuming the underlying object
	 * is of the correct ELAN TS firmware type. */
	remark_id_from_fw = fu_elan_ts_firmware_get_remark_id(FU_ELAN_TS_FIRMWARE(firmware));

	g_debug("remark ID from ROM: 0x%04x, Remark ID from FW: 0x%04x",
		remark_id_from_rom,
		remark_id_from_fw);

	/* validate Remark ID */
	if (remark_id_from_rom == ELAN_TS_REMARK_ID_NONE) {
		/* Non-Remark IC (0xFFFF): Bypass the verification */
		g_debug("non-Remark IC (0x%04x), bypassing check", remark_id_from_rom);
		return TRUE;
	}

	/* strict match required for Remark ICs */
	if (remark_id_from_rom != remark_id_from_fw) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "remark ID mismatched! (ROM: 0x%04x, FW: 0x%04x)",
			    remark_id_from_rom,
			    remark_id_from_fw);
		return FALSE;
	}

	g_debug("remark ID validation success");
	return TRUE;
}

/**
 * fu_elan_ts_iap_switch_to_boot_code:
 * @device: a #FuHidrawDevice
 * @recovery: %TRUE if the device is in recovery/corrupted state
 * @error: (nullable): a #GError, or %NULL
 *
 * Transitions the device from Application mode to Bootloader (IAP) mode using HID I/O.
 *
 * Returns: %TRUE for success, %FALSE for failure.
 **/
gboolean
fu_elan_ts_iap_switch_to_boot_code(FuHidrawDevice *device, gboolean recovery, GError **error)
{
	/* basic sanity checks */
	g_return_val_if_fail(FU_IS_HIDRAW_DEVICE(device), FALSE);
	g_return_val_if_fail((error == NULL) || (*error == NULL), FALSE);

	g_debug("switching to Bootloader mode (recovery=%s)", recovery ? "TRUE" : "FALSE");

	/* unlock flash */
	if (!fu_elan_ts_hid_unlock_flash(device, error)) {
		g_prefix_error_literal(error, "failed to unlock flash: ");
		return FALSE;
	}
	g_debug("unlock flash successfully");

	/* trigger IAP entry (bypassed in Recovery Mode) */
	if (!recovery) {
		if (!fu_elan_ts_hid_enter_iap_mode(device, error)) {
			g_prefix_error_literal(error, "failed to enter IAP mode: ");
			return FALSE;
		}

		g_debug("enter IAP mode successfully");
	} else {
		g_debug("device in recovery mode: already in IAP mode");
	}

	/* wait for hardware re-initialization (15ms) */
	fu_device_sleep(FU_DEVICE(device), 15);

	/* verify Bootloader responsiveness */
	if (!fu_elan_ts_hid_check_i2c_address(device, error)) {
		g_prefix_error_literal(error,
				       "device failed to respond if in boot mode after 15ms: ");
		return FALSE;
	}

	g_debug("switched to boot mode successfully");
	return TRUE;
}

/**
 * fu_elan_ts_iap_write_firmware_pages:
 * @device: (in): FuHidrawDevice instance
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
fu_elan_ts_iap_write_firmware_pages(FuHidrawDevice *device,
				    const guint8 *p_pages_buf,
				    gsize pages_buf_size,
				    GError **error)
{
	gsize offset = 0;
	gsize data_len = 0;
	gsize frame_count = 0;
	guint frame_index = 0;
	guint16 flash_write_response = 0;
	gboolean is_multi_page = (pages_buf_size > ELAN_TS_FW_PAGE_SIZE);

	/* basic sanity checks */
	g_return_val_if_fail(FU_IS_HIDRAW_DEVICE(device), FALSE);
	g_return_val_if_fail(p_pages_buf != NULL, FALSE);
	g_return_val_if_fail((error == NULL) || (*error == NULL), FALSE);

	/* validate that IAP buffer size is within limit and strictly page-aligned */
	if ((pages_buf_size == 0) || (pages_buf_size > (ELAN_TS_FW_PAGE_SIZE * 30)) ||
	    ((pages_buf_size % ELAN_TS_FW_PAGE_SIZE) != 0)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "invalid IAP buffer size: %" G_GSIZE_FORMAT
			    " (must be 1..30 pages and page-aligned)",
			    pages_buf_size);
		return FALSE;
	}

	/* calculate total frames required based on protocol limits */
	frame_count =
	    (pages_buf_size + ELAN_TS_HID_PAGE_FRAME_SIZE - 1) / ELAN_TS_HID_PAGE_FRAME_SIZE;

	/* transfer data frames sequentially */
	for (frame_index = 0; frame_index < frame_count; frame_index++) {
		offset = frame_index * ELAN_TS_HID_PAGE_FRAME_SIZE;
		data_len = MIN(pages_buf_size - offset, ELAN_TS_HID_PAGE_FRAME_SIZE);

		if (!fu_elan_ts_hid_write_frame_data(device,
						     (guint16)offset,
						     data_len,
						     p_pages_buf + offset,
						     error)) {
			g_prefix_error(error, "failed to send data frame %u: ", frame_index);
			return FALSE;
		}
	}

	/* request hardware to execute flash write operation */
	if (!fu_elan_ts_hid_send_flash_write_command(device, error)) {
		g_prefix_error_literal(error, "failed to send flash write command: ");
		return FALSE;
	}

	/* sleep to allow flash burning completion */
	if (is_multi_page)
		fu_device_sleep(FU_DEVICE(device), 360); /* long wait for multi-page block */
	else
		fu_device_sleep(FU_DEVICE(device),
				15); /* short wait for single page (e.g. Info Page) */

	/* read back and verify the execution response */
	if (!fu_elan_ts_hid_read_flash_write_response(device, &flash_write_response, error)) {
		g_prefix_error_literal(error, "failed to read flash write response: ");
		return FALSE;
	}

	g_debug("write firmware pages successful: %" G_GSIZE_FORMAT " bytes written",
		pages_buf_size);
	return TRUE;
}
