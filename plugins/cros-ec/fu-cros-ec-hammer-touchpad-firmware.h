/*
 * Copyright 2025 Hamed Elgizery
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-cros-ec-common.h"
#include "fu-cros-ec-struct.h"

#define FU_TYPE_CROS_EC_HAMMER_TOUCHPAD_FIRMWARE (fu_cros_ec_hammer_touchpad_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuCrosEcHammerTouchpadFirmware,
		     fu_cros_ec_hammer_touchpad_firmware,
		     FU,
		     CROS_EC_HAMMER_TOUCHPAD_FIRMWARE,
		     FuFirmware)

gboolean
fu_cros_ec_hammer_touchpad_firmware_validate_checksum(FuDevice *device,
						      FuCrosEcHammerTouchpadFirmware *self,
						      GError **error);
FuFirmware *
fu_cros_ec_hammer_touchpad_firmware_new(void);
