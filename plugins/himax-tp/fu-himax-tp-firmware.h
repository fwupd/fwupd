/*
 * Copyright 2026 Himax Company, Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_HIMAX_TP_FIRMWARE (fu_himax_tp_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuHimaxTpFirmware, fu_himax_tp_firmware, FU, HIMAX_TP_FIRMWARE, FuFirmware)

guint16
fu_himax_tp_firmware_get_cid(FuHimaxTpFirmware *self);
guint16
fu_himax_tp_firmware_get_vid(FuHimaxTpFirmware *self);
guint16
fu_himax_tp_firmware_get_pid(FuHimaxTpFirmware *self);
