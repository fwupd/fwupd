/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_VBE_SIMPLE_FIRMWARE (fu_vbe_simple_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuVbeSimpleFirmware,
		     fu_vbe_simple_firmware,
		     FU,
		     VBE_SIMPLE_FIRMWARE,
		     FuFirmware)

FuFirmware *
fu_vbe_simple_firmware_new(void);
