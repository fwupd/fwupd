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
guint16 fu_kinetic_mst_firmware_get_board_id(FuKineticMstFirmware *self);

typedef enum
{
    FU_KT_FW_IMG_IDX_ISP_DRV    = 0,
    FU_KT_FW_IMG_IDX_APP        = 1,
} FuKineticFwImgIdx;

