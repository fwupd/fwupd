/*
 * Copyright 2023 Denis Pynkin <denis.pynkin@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_QC_S5GEN2_FIRMWARE (fu_qc_s5gen2_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuQcS5gen2Firmware, fu_qc_s5gen2_firmware, FU, QC_S5GEN2_FIRMWARE, FuFirmware)

FuFirmware *
fu_qc_s5gen2_firmware_new(void);

guint8
fu_qc_s5gen2_firmware_get_protocol_version(FuQcS5gen2Firmware *self);

guint32
fu_qc_s5gen2_firmware_get_id(FuQcS5gen2Firmware *self);
