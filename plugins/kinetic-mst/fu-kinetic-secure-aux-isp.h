/*
 * Copyright (C) 2021 Jeffrey Lin <jlin@kinet-ic.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#define DPCD_KT_CONFIRMATION_BIT        0x80
#define DPCD_KT_COMMAND_MASK            0x7F

typedef enum {
	// Status
    KT_DPCD_CMD_STS_NONE                = 0x0,
    KT_DPCD_STS_INVALID_INFO            = 0x01,
    KT_DPCD_STS_CRC_FAILURE             = 0x02,
    KT_DPCD_STS_INVALID_IMAGE           = 0x03,
    KT_DPCD_STS_SECURE_ENABLED          = 0x04,
    KT_DPCD_STS_SECURE_DISABLED         = 0x05,
    KT_DPCD_STS_SPI_FLASH_FAILURE       = 0x06,

	// Command
	KT_DPCD_CMD_PREPARE_FOR_ISP_MODE    = 0x23,
    KT_DPCD_CMD_ENTER_CODE_LOADING_MODE = 0x24,
    KT_DPCD_CMD_EXECUTE_RAM_CODE        = 0x25,
    KT_DPCD_CMD_ENTER_FW_UPDATE_MODE    = 0x26,
    KT_DPCD_CMD_CHUNK_DATA_PROCESSED    = 0x27,
    KT_DPCD_CMD_INSTALL_IMAGES          = 0x28,
    KT_DPCD_CMD_RESET_SYSTEM            = 0x29,

    // Other command
    KT_DPCD_CMD_ENABLE_AUX_FORWARD      = 0x31,
    KT_DPCD_CMD_DISABLE_AUX_FORWARD     = 0x32,
    KT_DPCD_CMD_GET_ACTIVE_FLASH_BANK   = 0x33,

    // 0x70 ~ 0x7F are reserved for other usage
    KT_DPCD_CMD_RESERVED                = 0x7F,
} KineticSecureAuxIspCmdAndStatus;

