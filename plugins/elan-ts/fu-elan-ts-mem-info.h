/*
 * Copyright 2026 Elan Microelectronics Corporation <paul.liang@emc.com.tw>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

/**
 * SECTION: elan-ts-mem-info
 * @short_description: Memory addresses and constants for ELAN Touchscreen devices.
 *
 * This header defines the memory map and page constraints used for
 * firmware information and update tracking on ELAN touchscreen controllers.
 */

/*
 * Memory Page Size
 *
 * Defines the standard memory page size used by ELAN TS devices.
 * 128 bytes is equivalent to 64 words (0x40 words), where 1 word = 2 bytes.
 */
#define ELAN_TS_MEMORY_PAGE_SIZE 128

/* Information ROM Base Address */
#define ELAN_TS_MEM_INFO_ROM_ADDR 0x8000

/* Elan ROM Address of Remark ID */
#define ELAN_TS_MEM_REMARK_ID_ADDR 0x801F

/* Information Memory Page Address (The 2nd Info. Memory Page) */
#define ELAN_TS_MEM_INFO_PAGE_1_ADDR 0x8040

/* Information Page Address to Write */
#define ELAN_TS_MEM_INFO_PAGE_WRITE_ADDR 0x0040

/* Firmware Update Counter (Tracks the number of successful flash updates) */
#define ELAN_TS_MEM_UPDATE_COUNTER_ADDR 0x8060

/* Last Update Timestamp: Year (e.g., 2026) */
#define ELAN_TS_MEM_LAST_UPDATE_YEAR_ADDR 0x8061

/* Last Update Timestamp: Month and Day (Stored as 0xMMDD) */
#define ELAN_TS_MEM_LAST_UPDATE_MONTH_DAY_ADDR 0x8062

/* Last Update Timestamp: Hour and Minute (Stored as 0xHHMM) */
#define ELAN_TS_MEM_LAST_UPDATE_TIME_ADDR 0x8063

/* Information Page ROM FWID Address */
#define ELAN_TS_MEM_INFO_ROM_FWID_ADDR 0x8080

/* Default Remark ID value for ICs without a specific Remark ID */
#define ELAN_TS_REMARK_ID_NONE 0xFFFF

/* Parameter Addresses in Firmware Binary (Last Page) */
#define ELAN_TS_PARAM_ADDR_LAST_PAGE	  0xFFC0
#define ELAN_TS_PARAM_ADDR_GEN5_LAST_PAGE 0x7FC0
#define ELAN_TS_PARAM_ADDR_REMARK_ID	  0xFFFF
