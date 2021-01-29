/*
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-dfu-firmware.h"

#define FU_TYPE_DFUSE_FIRMWARE (fu_dfuse_firmware_get_type ())
G_DECLARE_DERIVABLE_TYPE (FuDfuseFirmware, fu_dfuse_firmware, FU, DFUSE_FIRMWARE, FuDfuFirmware)

struct _FuDfuseFirmwareClass
{
	FuDfuFirmwareClass		 parent_class;
};

FuFirmware		*fu_dfuse_firmware_new		(void);
