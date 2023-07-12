/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-firmware.h"
#include "fu-hid-struct.h"

#define FU_TYPE_HID_REPORT_ITEM (fu_hid_report_item_get_type())
G_DECLARE_FINAL_TYPE(FuHidReportItem, fu_hid_report_item, FU, HID_REPORT_ITEM, FuFirmware)

FuHidReportItem *
fu_hid_report_item_new(void);

FuHidItemKind
fu_hid_report_item_get_kind(FuHidReportItem *self);
guint32
fu_hid_report_item_get_value(FuHidReportItem *self);
