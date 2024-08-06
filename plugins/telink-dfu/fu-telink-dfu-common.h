/*
 * Copyright 2024 Mike Chang <mike.chang@telink-semi.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define DEVEL_STAGE_IGNORED 1

#define DFU_WRITE_METHOD_CHUNKS	       1
#define DFU_WRITE_METHOD_CUST_PACKET   2
#define DFU_WRITE_METHOD	       DFU_WRITE_METHOD_CUST_PACKET

#define CHAR_UUID_OTA	 "00010203-0405-0607-0809-0a0b0c0d2b12"
#define CHAR_UUID_BATT	 "00002a19-0000-1000-8000-00805f9b34fb"
#define CHAR_UUID_PNP	 "00002a50-0000-1000-8000-00805f9b34fb"
#define CHAR_UUID_FW_REV "00002a26-0000-1000-8000-00805f9b34fb"

guint32
fu_telink_dfu_parse_image_version(const gchar *version);
