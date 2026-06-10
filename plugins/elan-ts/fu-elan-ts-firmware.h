/*
 * Copyright 2026 Elan Microelectronics Corporation <paul.liang@emc.com.tw>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-elan-ts-struct.h"

#define FU_ELAN_TS_FIRMWARE_PAGE_SIZE 132 /* header(2) + data(128) + checksum(2) */

#define FU_TYPE_ELAN_TS_FIRMWARE (fu_elan_ts_firmware_get_type())

G_DECLARE_FINAL_TYPE(FuElanTsFirmware, fu_elan_ts_firmware, FU, ELAN_TS_FIRMWARE, FuFirmware)

FuElanTsFwType
fu_elan_ts_firmware_get_fw_type(FuElanTsFirmware *self) G_GNUC_NON_NULL(1);
FuElanTsDebugSetting
fu_elan_ts_firmware_get_debug_setting(FuElanTsFirmware *self) G_GNUC_NON_NULL(1);
guint16
fu_elan_ts_firmware_get_remark_id(FuElanTsFirmware *self) G_GNUC_NON_NULL(1);
