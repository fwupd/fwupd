/*
 * Copyright (C) 2021 Michael Cheng <michael.cheng@emc.com.tw>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_ELANFP_FIRMWARE (fu_elanfp_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuElanfpFirmware, fu_elanfp_firmware, FU, ELANFP_FIRMWARE, FuFirmware)

#define FU_ELANTP_FIRMWARE_IDX_FIRMWAREVERSION 0x00
#define FU_ELANTP_FIRMWARE_IDX_CFU_OFFER_A     0x72
#define FU_ELANTP_FIRMWARE_IDX_CFU_OFFER_B     0x73
#define FU_ELANTP_FIRMWARE_IDX_CFU_PAYLOAD_A   0x74
#define FU_ELANTP_FIRMWARE_IDX_CFU_PAYLOAD_B   0x75
#define FU_ELANTP_FIRMWARE_IDX_END	       0xFF
