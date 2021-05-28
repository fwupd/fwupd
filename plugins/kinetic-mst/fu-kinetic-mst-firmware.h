/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2021 Jeffrey Lin <jlin@kinet-ic.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-firmware.h"

#define FU_TYPE_KINETIC_MST_FIRMWARE (fu_kinetic_mst_firmware_get_type ())
G_DECLARE_FINAL_TYPE (FuKineticMstFirmware, fu_kinetic_mst_firmware, FU, KINETIC_MST_FIRMWARE, FuFirmware)

FuFirmware *fu_kinetic_mst_firmware_new(void);
guint32 fu_kinetic_mst_firmware_get_esm_payload_size(FuKineticMstFirmware *self);
guint32 fu_kinetic_mst_firmware_get_arm_app_code_size(FuKineticMstFirmware *self);
guint32 fu_kinetic_mst_firmware_get_app_init_data_size(FuKineticMstFirmware *self);
guint32 fu_kinetic_mst_firmware_get_cmdb_block_size(FuKineticMstFirmware *self);
gboolean fu_kinetic_mst_firmware_get_is_fw_esm_xip_enabled(FuKineticMstFirmware *self);

typedef enum
{
    FU_KT_FW_IMG_IDX_ISP_DRV    = 0,
    FU_KT_FW_IMG_IDX_APP_FW     = 1,
} FuKineticFwImgIdx;

