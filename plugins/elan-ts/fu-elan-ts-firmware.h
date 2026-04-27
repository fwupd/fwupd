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
 * 0      | Address (Prefix) | 1 Word (2 Bytes, Little Endian)
 * 2      | Page Data        | 64 Words (128 Bytes)
 * 130    | Checksum (Suffix)| 1 Word (2 Bytes, Little Endian)
 * --------------------------------------------------------
 * Total: 66 Words (132 Bytes)
 */

/* Firmware pagination constants */
#define ELAN_TS_FW_PAGE_DATA_SIZE	  128 /* Bytes */
#define ELAN_TS_FW_PAGE_DATA_SIZE_IN_WORD (ELAN_TS_FW_PAGE_DATA_SIZE / 2)
#define ELAN_TS_FW_PAGE_SIZE		  132 /* Header(2) + Data(128) + Checksum(2) */
#define ELAN_TS_FW_PAGES_PER_BLOCK	  30  /* Maximum pages per write cycle */

#define FU_TYPE_ELAN_TS_FIRMWARE (fu_elan_ts_firmware_get_type())

G_DECLARE_FINAL_TYPE(FuElanTsFirmware, fu_elan_ts_firmware, FU, ELAN_TS_FIRMWARE, FuFirmware)

FuElanTsFwType
fu_elan_ts_firmware_get_fw_type(FuElanTsFirmware *self) G_GNUC_NON_NULL(1);

FuElanTsDebugSetting
fu_elan_ts_firmware_get_debug_setting(FuElanTsFirmware *self) G_GNUC_NON_NULL(1);

guint32
fu_elan_ts_firmware_get_bin_size(FuElanTsFirmware *self) G_GNUC_NON_NULL(1);

guint
fu_elan_ts_firmware_get_page_count(FuElanTsFirmware *self) G_GNUC_NON_NULL(1);

guint16
fu_elan_ts_firmware_get_remark_id(FuElanTsFirmware *self) G_GNUC_NON_NULL(1);

gboolean
fu_elan_ts_firmware_get_page(FuElanTsFirmware *self,
			     guint base_page_index,
			     guint page_count,
			     guint8 *p_page_buf,
			     gsize page_buf_size,
			     GError **error) G_GNUC_NON_NULL(1, 4);
