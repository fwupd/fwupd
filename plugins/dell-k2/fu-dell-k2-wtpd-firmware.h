/*
 * Copyright 2024 Dell Technologies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_DELL_K2_WTPD_FIRMWARE (fu_dell_k2_wtpd_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuDellK2WtpdFirmware,
		     fu_dell_k2_wtpd_firmware,
		     FU,
		     DELL_K2_WTPD_FIRMWARE,
		     FuFirmware)

FuFirmware *
fu_dell_k2_wtpd_firmware_new(void);
