/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-firmware.h"

#define FU_TYPE_EFI_VOLUME (fu_efi_volume_get_type())
G_DECLARE_DERIVABLE_TYPE(FuEfiVolume, fu_efi_volume, FU, EFI_VOLUME, FuFirmware)

struct _FuEfiVolumeClass {
	FuFirmwareClass parent_class;
};

FuFirmware *
fu_efi_volume_new(void);
