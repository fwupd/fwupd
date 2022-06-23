/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2022 Intel
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-firmware.h"

#define FU_TYPE_IFWI_CPD_FIRMWARE (fu_ifwi_cpd_firmware_get_type())
G_DECLARE_DERIVABLE_TYPE(FuIfwiCpdFirmware, fu_ifwi_cpd_firmware, FU, IFWI_CPD_FIRMWARE, FuFirmware)

struct _FuIfwiCpdFirmwareClass {
	FuFirmwareClass parent_class;
};

/**
 * FU_IFWI_CPD_FIRMWARE_IDX_MANIFEST:
 *
 * The index for the IFWI manifest image.
 *
 * Since: 1.8.2
 **/
#define FU_IFWI_CPD_FIRMWARE_IDX_MANIFEST 0x0

/**
 * FU_IFWI_CPD_FIRMWARE_IDX_METADATA:
 *
 * The index for the IFWI metadata image.
 *
 * Since: 1.8.2
 **/
#define FU_IFWI_CPD_FIRMWARE_IDX_METADATA 0x1

/**
 * FU_IFWI_CPD_FIRMWARE_IDX_MODULEDATA_IDX:
 *
 * The index for the IFWI module data image.
 *
 * Since: 1.8.2
 **/
#define FU_IFWI_CPD_FIRMWARE_IDX_MODULEDATA_IDX 0x2

FuFirmware *
fu_ifwi_cpd_firmware_new(void);
