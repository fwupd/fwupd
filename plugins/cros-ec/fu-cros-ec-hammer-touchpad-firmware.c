/*
 * Copyright 2025 Hamed Elgizery
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-cros-ec-common.h"
#include "fu-cros-ec-firmware.h"
#include "fu-cros-ec-hammer-touchpad-firmware.h"
#include "fu-cros-ec-hammer-touchpad.h"

struct _FuCrosEcHammerTouchpadFirmware {
	FuFirmware parent_instance;
};

G_DEFINE_TYPE(FuCrosEcHammerTouchpadFirmware, fu_cros_ec_hammer_touchpad_firmware, FU_TYPE_FIRMWARE)

static void
fu_cros_ec_hammer_touchpad_firmware_init(FuCrosEcHammerTouchpadFirmware *self)
{
}

static void
fu_cros_ec_hammer_touchpad_firmware_class_init(FuCrosEcHammerTouchpadFirmwareClass *klass)
{
}

FuFirmware *
fu_cros_ec_hammer_touchpad_firmware_new(void)
{
	return g_object_new(FU_TYPE_CROS_EC_HAMMER_TOUCHPAD_FIRMWARE, NULL);
}
