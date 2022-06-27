/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2022 Intel
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-firmware.h"

#define FU_TYPE_IFWI_FPT_FIRMWARE (fu_ifwi_fpt_firmware_get_type())
G_DECLARE_DERIVABLE_TYPE(FuIfwiFptFirmware, fu_ifwi_fpt_firmware, FU, IFWI_FPT_FIRMWARE, FuFirmware)

struct _FuIfwiFptFirmwareClass {
	FuFirmwareClass parent_class;
};

/**
 * FU_IFWI_FPT_FIRMWARE_IDX_INFO:
 *
 * The index for the IFWI info image.
 *
 * Since: 1.8.2
 **/
#define FU_IFWI_FPT_FIRMWARE_IDX_INFO 0x4f464e49

/**
 * FU_IFWI_FPT_FIRMWARE_IDX_FWIM:
 *
 * The index for the IFWI firmware image.
 *
 * Since: 1.8.2
 **/
#define FU_IFWI_FPT_FIRMWARE_IDX_FWIM 0x4d495746

/**
 * FU_IFWI_FPT_FIRMWARE_IDX_IMGI:
 *
 * The index for the IFWI image instance.
 *
 * Since: 1.8.2
 **/
#define FU_IFWI_FPT_FIRMWARE_IDX_IMGI 0x49474d49

/**
 * FU_IFWI_FPT_FIRMWARE_IDX_SDTA:
 *
 * The index for the IFWI firmware data image.
 *
 * Since: 1.8.2
 **/
#define FU_IFWI_FPT_FIRMWARE_IDX_SDTA 0x41544447

/**
 * FU_IFWI_FPT_FIRMWARE_IDX_CKSM:
 *
 * The index for the IFWI checksum image.
 *
 * Since: 1.8.2
 **/
#define FU_IFWI_FPT_FIRMWARE_IDX_CKSM 0x4d534b43

FuFirmware *
fu_ifwi_fpt_firmware_new(void);
