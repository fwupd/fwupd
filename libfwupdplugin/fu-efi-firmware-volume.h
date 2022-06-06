/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-firmware.h"

#define FU_TYPE_EFI_FIRMWARE_VOLUME (fu_efi_firmware_volume_get_type())
G_DECLARE_DERIVABLE_TYPE(FuEfiFirmwareVolume,
			 fu_efi_firmware_volume,
			 FU,
			 EFI_FIRMWARE_VOLUME,
			 FuFirmware)

struct _FuEfiFirmwareVolumeClass {
	FuFirmwareClass parent_class;
};

FuFirmware *
fu_efi_firmware_volume_new(void);
