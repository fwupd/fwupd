/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-firmware.h"

#define FU_TYPE_USWID_FIRMWARE (fu_uswid_firmware_get_type())
G_DECLARE_DERIVABLE_TYPE(FuUswidFirmware, fu_uswid_firmware, FU, USWID_FIRMWARE, FuFirmware)

struct _FuUswidFirmwareClass {
	FuFirmwareClass parent_class;
};

FuFirmware *
fu_uswid_firmware_new(void);
