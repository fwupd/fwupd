/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-firmware.h"

#define FU_TYPE_JSON_FIRMWARE (fu_json_firmware_get_type())
G_DECLARE_DERIVABLE_TYPE(FuJsonFirmware, fu_json_firmware, FU, JSON_FIRMWARE, FuFirmware)

struct _FuJsonFirmwareClass {
	FuFirmwareClass parent_class;
};
