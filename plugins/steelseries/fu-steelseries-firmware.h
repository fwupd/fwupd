/*
 * Copyright (C) 2022 Gaël PORTAY <gael.portay@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_STEELSERIES_FIRMWARE (fu_steelseries_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuSteelseriesFirmware,
		     fu_steelseries_firmware,
		     FU,
		     STEELSERIES_FIRMWARE,
		     FuFirmware)

FuFirmware *
fu_steelseries_firmware_new(void);
