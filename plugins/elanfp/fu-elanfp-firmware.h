/*
 * Copyright (C) 2021 Michael Cheng <michael.cheng@emc.com.tw>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_ELANFP_FIRMWARE (fu_elanfp_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuElanfpFirmware, fu_elanfp_firmware, FU, ELANFP_FIRMWARE, FuFirmware)

FuFirmware *
fu_elanfp_firmware_new(void);
