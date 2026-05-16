/*
 * Copyright 2026 Elan Microelectronics Corporation <paul.liang@emc.com.tw>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-elan-ts-struct.h"

/**
 * ELAN Firmware Page Structure:
 * --------------------------------------------------------
 * Offset | Component        | Size
 * --------------------------------------------------------
 * 0      | Address (Prefix) | 1 Word  (2 Bytes, Big Endian)
 * 2      | Page Data        | 64 Words (128 Bytes)
 * 130    | Checksum (Suffix)| 1 Word  (2 Bytes, Big Endian)
 * --------------------------------------------------------
 * Total:                     66 Words (132 Bytes)
 */

/* Firmware pagination constants */
#define ELAN_TS_FW_PAGE_DATA_SIZE         128 /* Bytes */
#define ELAN_TS_FW_PAGE_DATA_SIZE_IN_WORD (ELAN_TS_FW_PAGE_DATA_SIZE / 2)
#define ELAN_TS_FW_PAGE_SIZE              132 /* Header(2) + Data(128) + Checksum(2) */
#define ELAN_TS_FW_PAGES_PER_BLOCK        30  /* Maximum pages per write cycle */

#define FU_TYPE_ELAN_TS_FIRMWARE (fu_elan_ts_firmware_get_type())

G_DECLARE_FINAL_TYPE(FuElanTsFirmware,
                     fu_elan_ts_firmware,
                     FU,
                     ELAN_TS_FIRMWARE,
                     FuFirmware)

/* Accessors for parsed firmware properties */

/**
 * elan_ts_firmware_get_fw_type:
 * @self: a #FuElanTsFirmware
 *
 * Gets the firmware type specified in the binary header.
 *
 * Returns: a #FuElanTsFwType, e.g. %FU_ELAN_TS_FW_TYPE_EKT
 **/
FuElanTsFwType
elan_ts_firmware_get_fw_type(FuElanTsFirmware *self) G_GNUC_NON_NULL(1);

/**
 * elan_ts_firmware_get_debug_setting:
 * @self: a #FuElanTsFirmware
 *
 * Gets the debug setting bitmask from the firmware header.
 * These settings control logging behavior and update policy overrides.
 *
 * Returns: a #FuElanTsDebugSetting bitmask
 **/
FuElanTsDebugSetting
elan_ts_firmware_get_debug_setting(FuElanTsFirmware *self) G_GNUC_NON_NULL(1);

/**
 * elan_ts_firmware_get_bin_size:
 * @self: a #FuElanTsFirmware
 *
 * Gets the binary size extracted from the ELAN firmware header.
 *
 * Returns: the size in bytes, or 0 if invalid.
 **/
guint32
elan_ts_firmware_get_bin_size(FuElanTsFirmware *self) G_GNUC_NON_NULL(1);

/**
 * elan_ts_firmware_get_page_count:
 * @self: a #FuElanTsFirmware
 *
 * Calculates the number of 132-byte pages in the firmware binary.
 *
 * Returns: the number of pages, or 0 on error.
 **/
guint
elan_ts_firmware_get_page_count(FuElanTsFirmware *self) G_GNUC_NON_NULL(1);

/**
 * elan_ts_firmware_get_remark_id:
 * @self: a #FuElanTsFirmware
 *
 * Gets the remark ID extracted from the firmware payload.
 * This ID is used to verify hardware compatibility before flashing.
 *
 * Returns: a remark ID, e.g. 0x1234
 **/
guint16
elan_ts_firmware_get_remark_id(FuElanTsFirmware *self) G_GNUC_NON_NULL(1);

/**
 * elan_ts_firmware_get_page:
 * @self: a #FuElanTsFirmware
 * @base_page_index: the index of the first page to retrieve
 * @page_count: number of pages to retrieve
 * @p_page_buf: (out): destination buffer to store the formatted pages
 * @page_buf_size: total size of the destination buffer
 * @error: (nullable): a #GError, or %NULL
 *
 * Extracts pre-formatted 132-byte pages from the firmware binary payload.
 *
 * Returns: %TRUE for success, %FALSE for failure.
 **/
gboolean
elan_ts_firmware_get_page(FuElanTsFirmware *self,
                         guint base_page_index,
                         guint page_count,
                         guint8 *p_page_buf,
                         gsize page_buf_size,
                         GError **error) G_GNUC_NON_NULL(1, 4);

