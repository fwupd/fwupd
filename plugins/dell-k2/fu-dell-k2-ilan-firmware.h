/*
 * Copyright 2024 Dell Technologies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_DELL_K2_ILAN_FIRMWARE (fu_dell_k2_ilan_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuDellK2IlanFirmware,
		     fu_dell_k2_ilan_firmware,
		     FU,
		     DELL_K2_ILAN_FIRMWARE,
		     FuFirmware)

FuFirmware *
fu_dell_k2_ilan_firmware_new(void);
