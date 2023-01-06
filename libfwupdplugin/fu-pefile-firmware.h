/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-firmware.h"

#define FU_TYPE_PEFILE_FIRMWARE (fu_pefile_firmware_get_type())
G_DECLARE_DERIVABLE_TYPE(FuPefileFirmware, fu_pefile_firmware, FU, PEFILE_FIRMWARE, FuFirmware)

struct _FuPefileFirmwareClass {
	FuFirmwareClass parent_class;
};

FuFirmware *
fu_pefile_firmware_new(void);
