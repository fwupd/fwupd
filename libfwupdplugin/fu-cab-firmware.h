/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-firmware.h"

#define FU_TYPE_CAB_FIRMWARE (fu_cab_firmware_get_type())

G_DECLARE_DERIVABLE_TYPE(FuCabFirmware, fu_cab_firmware, FU, CAB_FIRMWARE, FuFirmware)

struct _FuCabFirmwareClass {
	FuFirmwareClass parent_class;
};

gboolean
fu_cab_firmware_get_compressed(FuCabFirmware *self) G_GNUC_NON_NULL(1);
void
fu_cab_firmware_set_compressed(FuCabFirmware *self, gboolean compressed) G_GNUC_NON_NULL(1);

FuCabFirmware *
fu_cab_firmware_new(void) G_GNUC_WARN_UNUSED_RESULT;
