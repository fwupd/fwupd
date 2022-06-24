/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-firmware.h"

#define FU_TYPE_LINEAR_FIRMWARE (fu_linear_firmware_get_type())
G_DECLARE_DERIVABLE_TYPE(FuLinearFirmware, fu_linear_firmware, FU, LINEAR_FIRMWARE, FuFirmware)

struct _FuLinearFirmwareClass {
	FuFirmwareClass parent_class;
};

FuFirmware *
fu_linear_firmware_new(GType image_gtype);
GType
fu_linear_firmware_get_image_gtype(FuLinearFirmware *self);
