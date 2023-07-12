/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-firmware.h"

#define FU_TYPE_HID_REPORT (fu_hid_report_get_type())
G_DECLARE_FINAL_TYPE(FuHidReport, fu_hid_report, FU, HID_REPORT, FuFirmware)

FuHidReport *
fu_hid_report_new(void);
