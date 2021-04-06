/*
 * Copyright (C) 2021 Xiaotian Cui <xtcui@analogixsemi.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */
#pragma once

#include "fu-firmware.h"

#include "fu-analogix-usbc-common.h"

#define FU_TYPE_ANALOGIX_USBC_FIRMWARE (fu_analogix_usbc_firmware_get_type ())
G_DECLARE_FINAL_TYPE (FuAnalogixUsbcFirmware, fu_analogix_usbc_firmware, FU,\
		      ANALOGIX_USBC_FIRMWARE, FuFirmware)

FuFirmware		*fu_analogix_usbc_firmware_new		(void);
