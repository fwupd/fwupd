/*
 * Copyright 2024 Richard hughes <Richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_UEFI_SBAT_FIRMWARE (fu_uefi_sbat_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuUefiSbatFirmware,
		     fu_uefi_sbat_firmware,
		     FU,
		     UEFI_SBAT_FIRMWARE,
		     FuCsvFirmware)

FuFirmware *
fu_uefi_sbat_firmware_new(void);
