/*
 * Copyright 2026 SHENZHEN Betterlife Electronic CO.LTD <xiaomaoting@blestech.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define REPORT_ID                0x0E
#define SET_REPORT_PACK_FIX_SIZE 0x06
/* fixed 96K fw size */
#define FW_SIZE                  0x18000
#define FW_PAGE_SIZE             0x200
/* 16K BOOT FW size*/
#define BOOT_SIZE                0x4000
#define PROGRAM_PACK_LEN         24
#define APP_CONFIG_PAGE          96
#define FW_VER_LEN 				 7
#define BIN_VER_ADDR             0xC02A
