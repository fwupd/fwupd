/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 * Copyright 2021 Jeffrey Lin <jlin@kinet-ic.com>
 * Copyright 2022 Hai Su <hsu@kinet-ic.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_KINETIC_DP_PUMA_FIRMWARE (fu_kinetic_dp_puma_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuKineticDpPumaFirmware,
		     fu_kinetic_dp_puma_firmware,
		     FU,
		     KINETIC_DP_PUMA_FIRMWARE,
		     FuFirmware)
