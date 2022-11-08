/*
 * Copyright (C) 2021 Texas Instruments Incorporated
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+ OR MIT
 */

#include "config.h"

#include "fu-ti-tps6598x-common.h"

const gchar *
fu_ti_tps6598x_device_sfwi_strerror(guint8 res)
{
	if (res == TI_TPS6598X_SFWI_FAIL_FLASH_ERROR_OR_BUSY)
		return "flash-error-or-busy";
	if (res == TI_TPS6598X_SFWI_FAIL_FLASH_INVALID_ADDRESS)
		return "flash-invalid-address";
	if (res == TI_TPS6598X_SFWI_FAIL_LAST_BOOT_WAS_UART)
		return "last-boot-was-uart";
	if (res == TI_TPS6598X_SFWI_FAIL_SFWI_AFTER_COMPLETE)
		return "sfwi-after-complete";
	if (res == TI_TPS6598X_SFWI_FAIL_NO_VALID_FLASH_REGION)
		return "no-valid-flash-region";
	return NULL;
}

const gchar *
fu_ti_tps6598x_device_sfwd_strerror(guint8 res)
{
	if (res == TI_TPS6598X_SFWD_FAIL_FLASH_ERASE_WRITE_ERROR)
		return "flash-erase-write-error";
	if (res == TI_TPS6598X_SFWD_FAIL_SFWI_NOT_RUN_FIRST)
		return "sfwi-not-run-first";
	if (res == TI_TPS6598X_SFWD_FAIL_TOO_MUCH_DATA)
		return "too-much-data";
	if (res == TI_TPS6598X_SFWD_FAIL_ID_NOT_IN_HEADER)
		return "id-not-in-header";
	if (res == TI_TPS6598X_SFWD_FAIL_BINARY_TOO_LARGE)
		return "binary-too-large";
	if (res == TI_TPS6598X_SFWD_FAIL_DEVICE_ID_MISMATCH)
		return "device-id-mismatch";
	if (res == TI_TPS6598X_SFWD_FAIL_FLASH_ERROR_READ_ONLY)
		return "flash-error-read-only";
	return NULL;
}

const gchar *
fu_ti_tps6598x_device_sfws_strerror(guint8 res)
{
	if (res == TI_TPS6598X_SFWS_FAIL_FLASH_ERASE_WRITE_ERROR)
		return "flash-erase-write-error";
	if (res == TI_TPS6598X_SFWS_FAIL_SFWD_NOT_RUN_OR_NO_KEY_EXISTS)
		return "sfwd-not-run-or-no-key-exists";
	if (res == TI_TPS6598X_SFWS_FAIL_TOO_MUCH_DATA)
		return "too-much-data";
	if (res == TI_TPS6598X_SFWS_FAIL_CRC_FAIL)
		return "crc-fail";
	if (res == TI_TPS6598X_SFWS_FAIL_DID_CHECK_FAIL)
		return "did-check-fail";
	if (res == TI_TPS6598X_SFWS_FAIL_VERSION_CHECK_FAIL)
		return "version-check-fail";
	if (res == TI_TPS6598X_SFWS_FAIL_NO_HASH_MATCH_RULE_SATISFIED)
		return "no-hash-match-rule-satisfied";
	if (res == TI_TPS6598X_SFWS_FAIL_ENGR_FW_UPDATE_ATTEMPT_WHILE_RUNNING_PROD)
		return "engr-fw-update-attempt-while-running-prod";
	if (res == TI_TPS6598X_SFWS_FAIL_INCOMPATIBLE_ROM_VERSION)
		return "incompatible-rom-version";
	if (res == TI_TPS6598X_SFWS_FAIL_CRC_BUSY)
		return "crc-busy";
	return NULL;
}

gboolean
fu_ti_tps6598x_byte_array_is_nonzero(GByteArray *buf)
{
	if (buf->len == 0)
		return FALSE;
	for (guint j = 1; j < buf->len; j++) {
		if (buf->data[j] != 0x0)
			return TRUE;
	}
	return FALSE;
}
