/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-firmware.h"

#define FU_TYPE_ZIP_FIRMWARE (fu_zip_firmware_get_type())

G_DECLARE_DERIVABLE_TYPE(FuZipFirmware, fu_zip_firmware, FU, ZIP_FIRMWARE, FuFirmware)

struct _FuZipFirmwareClass {
	FuFirmwareClass parent_class;
};

FuFirmware *
fu_zip_firmware_new(void) G_GNUC_WARN_UNUSED_RESULT;
