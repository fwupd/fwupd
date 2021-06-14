/*
 * Copyright (C) 2021 Xiaotian Cui <xtcui@analogixsemi.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */
#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_ANALOGIX_FIRMWARE (fu_analogix_firmware_get_type ())
G_DECLARE_FINAL_TYPE (FuAnalogixFirmware, fu_analogix_firmware, FU,\
		      ANALOGIX_FIRMWARE, FuIhexFirmware)

FuFirmware		*fu_analogix_firmware_new		(void);
