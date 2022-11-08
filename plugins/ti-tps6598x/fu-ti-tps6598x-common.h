/*
 * Copyright (C) 2021 Texas Instruments Incorporated
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+ OR MIT
 */

#pragma once

#include <fwupdplugin.h>

/* registers */
#define TI_TPS6598X_REGISTER_TBT_VID	      0x00 /* ro, 4 bytes -- Intel assigned */
#define TI_TPS6598X_REGISTER_TBT_DID	      0x01 /* ro, 4 bytes -- Intel assigned */
#define TI_TPS6598X_REGISTER_PROTO_VER	      0x02 /* ro, 4 bytes */
#define TI_TPS6598X_REGISTER_MODE	      0x03 /* ro, 4 bytes */
#define TI_TPS6598X_REGISTER_TYPE	      0x04 /* ro, 4 bytes */
#define TI_TPS6598X_REGISTER_UID	      0x05 /* ro, 16 bytes */
#define TI_TPS6598X_REGISTER_OUID	      0x06 /* ro, 8 bytes */
#define TI_TPS6598X_REGISTER_CMD1	      0x08 /* ro, 4CC */
#define TI_TPS6598X_REGISTER_DATA1	      0x09 /* rw, 64 bytes */
#define TI_TPS6598X_REGISTER_VERSION	      0x0F /* rw, 4 bytes */
#define TI_TPS6598X_REGISTER_CMD2	      0x10 /* ro, 4CC */
#define TI_TPS6598X_REGISTER_DATA2	      0x11 /* rw, 64 bytes */
#define TI_TPS6598X_REGISTER_CMD3	      0x1E /* ro, variable */
#define TI_TPS6598X_REGISTER_DATA3	      0x1F /* ro, variable */
#define TI_TPS6598X_REGISTER_OTP_CONFIG	      0x2D /* ro, 12 bytes */
#define TI_TPS6598X_REGISTER_BUILD_IDENTIFIER 0x2E /* ro, 64 bytes */
#define TI_TPS6598X_REGISTER_DEVICE_INFO      0x2F /* ro, 47 bytes */

#define TI_TPS6598X_SFWI_SUCCESS		    0x0
#define TI_TPS6598X_SFWI_FAIL_FLASH_ERROR_OR_BUSY   0x4
#define TI_TPS6598X_SFWI_FAIL_FLASH_INVALID_ADDRESS 0x5
#define TI_TPS6598X_SFWI_FAIL_LAST_BOOT_WAS_UART    0x6
#define TI_TPS6598X_SFWI_FAIL_SFWI_AFTER_COMPLETE   0x7
#define TI_TPS6598X_SFWI_FAIL_NO_VALID_FLASH_REGION 0x8
#define TI_TPS6598X_SFWI_FAIL_UNKNOWN_ERROR	    0xF

#define TI_TPS6598X_SFWD_SUCCESS		      0x0
#define TI_TPS6598X_SFWD_FAIL_FLASH_ERASE_WRITE_ERROR 0x4
#define TI_TPS6598X_SFWD_FAIL_SFWI_NOT_RUN_FIRST      0x6
#define TI_TPS6598X_SFWD_FAIL_TOO_MUCH_DATA	      0x7
#define TI_TPS6598X_SFWD_FAIL_ID_NOT_IN_HEADER	      0x8
#define TI_TPS6598X_SFWD_FAIL_BINARY_TOO_LARGE	      0x9
#define TI_TPS6598X_SFWD_FAIL_DEVICE_ID_MISMATCH      0xA
#define TI_TPS6598X_SFWD_FAIL_FLASH_ERROR_READ_ONLY   0xD
#define TI_TPS6598X_SFWD_FAIL_UNKNOWN_ERROR	      0xF

#define TI_TPS6598X_SFWS_SUCCESS					0x0
#define TI_TPS6598X_SFWS_FAIL_FLASH_ERASE_WRITE_ERROR			0x4
#define TI_TPS6598X_SFWS_FAIL_SFWD_NOT_RUN_OR_NO_KEY_EXISTS		0x6
#define TI_TPS6598X_SFWS_FAIL_TOO_MUCH_DATA				0x7
#define TI_TPS6598X_SFWS_FAIL_CRC_FAIL					0x8
#define TI_TPS6598X_SFWS_FAIL_DID_CHECK_FAIL				0x9
#define TI_TPS6598X_SFWS_FAIL_VERSION_CHECK_FAIL			0xA
#define TI_TPS6598X_SFWS_FAIL_NO_HASH_MATCH_RULE_SATISFIED		0xB
#define TI_TPS6598X_SFWS_FAIL_ENGR_FW_UPDATE_ATTEMPT_WHILE_RUNNING_PROD 0xC
#define TI_TPS6598X_SFWS_FAIL_INCOMPATIBLE_ROM_VERSION			0xD
#define TI_TPS6598X_SFWS_FAIL_CRC_BUSY					0xE
#define TI_TPS6598X_SFWS_FAIL_UNKNOWN_ERROR				0xF

#define FU_TI_TPS6598X_PD_MAX 2 /* devices */

const gchar *
fu_ti_tps6598x_device_sfwi_strerror(guint8 res);
const gchar *
fu_ti_tps6598x_device_sfwd_strerror(guint8 res);
const gchar *
fu_ti_tps6598x_device_sfws_strerror(guint8 res);
gboolean
fu_ti_tps6598x_byte_array_is_nonzero(GByteArray *buf);
