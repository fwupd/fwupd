/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-fdt-image.h"
#include "fu-firmware.h"

#define FU_TYPE_FDT_FIRMWARE (fu_fdt_firmware_get_type())
G_DECLARE_DERIVABLE_TYPE(FuFdtFirmware, fu_fdt_firmware, FU, FDT_FIRMWARE, FuFirmware)

struct _FuFdtFirmwareClass {
	FuFirmwareClass parent_class;
};

FuFirmware *
fu_fdt_firmware_new(void);
guint32
fu_fdt_firmware_get_cpuid(FuFdtFirmware *self);
void
fu_fdt_firmware_set_cpuid(FuFdtFirmware *self, guint32 cpuid);
FuFdtImage *
fu_fdt_firmware_get_image_by_path(FuFdtFirmware *self, const gchar *path, GError **error);
