/*
 * Copyright 2025 Hamed Elgizery
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-cros-ec-common.h"
#include "fu-cros-ec-firmware.h"
#include "fu-cros-ec-hammer-touchpad-firmware.h"

struct _FuCrosEcHammerTouchpadFirmware {
	FuFirmware parent_instance;
};

G_DEFINE_TYPE(FuCrosEcHammerTouchpadFirmware, fu_cros_ec_hammer_touchpad_firmware, FU_TYPE_FIRMWARE)

gboolean
fu_cros_ec_hammer_touchpad_firmware_validate_checksum(FuCrosEcHammerTouchpadFirmware *self,
						      GError **error)
{
	/* success */
	return TRUE;
}

static void
fu_cros_ec_hammer_touchpad_firmware_init(FuCrosEcHammerTouchpadFirmware *self)
{
}

static void
fu_cros_ec_hammer_touchpad_firmware_finalize(GObject *object)
{
	FuCrosEcFirmware *self = FU_CROS_EC_FIRMWARE(object);
	G_OBJECT_CLASS(fu_cros_ec_hammer_touchpad_firmware_parent_class)->finalize(object);
}

static void
fu_cros_ec_hammer_touchpad_firmware_class_init(FuCrosEcHammerTouchpadFirmwareClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_cros_ec_hammer_touchpad_firmware_finalize;
}

FuFirmware *
fu_cros_ec_hammer_touchpad_firmware_new(void)
{
	return g_object_new(FU_TYPE_CROS_EC_HAMMER_TOUCHPAD_FIRMWARE, NULL);
}
