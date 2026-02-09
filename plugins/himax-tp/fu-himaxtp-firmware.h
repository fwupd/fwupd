/*
 * Copyright 2026 Himax Company, Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_HIMAXTP_FIRMWARE (fu_himaxtp_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuHimaxtpFirmware, fu_himaxtp_firmware, FU, HIMAXTP_FIRMWARE, FuFirmware)

FuFirmware *
fu_himaxtp_firmware_new(void);

guint16
fu_himaxtp_firmware_get_fw_ver(FuHimaxtpFirmware *self);
guint16
fu_himaxtp_firmware_get_cid(FuHimaxtpFirmware *self);
guint16
fu_himaxtp_firmware_get_vid(FuHimaxtpFirmware *self);
guint16
fu_himaxtp_firmware_get_pid(FuHimaxtpFirmware *self);
guint8
fu_himaxtp_firmware_get_tp_cfg_ver(FuHimaxtpFirmware *self);
guint8
fu_himaxtp_firmware_get_dd_cfg_ver(FuHimaxtpFirmware *self);
gchar *
fu_himaxtp_firmware_get_ic_id(FuHimaxtpFirmware *self);
gchar *
fu_himaxtp_firmware_get_ic_id_mod(FuHimaxtpFirmware *self);
