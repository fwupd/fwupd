/*
 * Copyright 2024 Mike Chang <mike.chang@telink-semi.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#define DEBUG_ARCHIVE	    1
#define DEVEL_STAGE_IGNORED 1
#define DEBUG_GATT_CHAR_RW  0
// prepare_firmware and fireware_gtype are mutual exclusive methods
#define USE_FIRMWARE_GTYPE	1

#define DFU_WRITE_METHOD_CHUNKS	       1
#define DFU_WRITE_METHOD_CUST_PACKET   2
#define DFU_WRITE_METHOD	       DFU_WRITE_METHOD_CUST_PACKET
#define DEBUG_WRITE_METHOD_CUST_PACKET 0

#define CHAR_UUID_OTA	 "00010203-0405-0607-0809-0a0b0c0d2b12"
#define CHAR_UUID_BATT	 "00002a19-0000-1000-8000-00805f9b34fb"
#define CHAR_UUID_PNP	 "00002a50-0000-1000-8000-00805f9b34fb"
#define CHAR_UUID_FW_REV "00002a26-0000-1000-8000-00805f9b34fb"
