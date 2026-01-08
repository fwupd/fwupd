/*
 * Copyright 2025 Raydium.inc <Maker.Tsai@rad-ic.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define VENDOR_ID					0x2386
#define GET_SYS_FW_VERSION_NUM 		1
#define MCU_MEM						1

#define I2C_BUF_SIZE 				64
#define HIDI2C_WRITE_SIZE			32
#define CRC_LEN           			4
#define HIDI2C_CHK_IDX				61

#define HIDI2C_WRITE_MAX_LENGTH     49

#define RM_FW_PAGE_SIZE		128

#define RETRY_NUM			10
#define RETRY_NUM_MAX		30

#define FLASH_SECTOR_SIZE			4096
