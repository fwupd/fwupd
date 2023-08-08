/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-firmware.h"

#define FU_TYPE_ELF_FIRMWARE (fu_elf_firmware_get_type())
G_DECLARE_DERIVABLE_TYPE(FuElfFirmware, fu_elf_firmware, FU, ELF_FIRMWARE, FuFirmware)

struct _FuElfFirmwareClass {
	FuFirmwareClass parent_class;
};

FuFirmware *
fu_elf_firmware_new(void);
