/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-firmware.h"

#define FU_TYPE_CAB_FIRMWARE (fu_cab_firmware_get_type())

G_DECLARE_DERIVABLE_TYPE(FuCabFirmware, fu_cab_firmware, FU, CAB_FIRMWARE, FuFirmware)

struct _FuCabFirmwareClass {
	FuFirmwareClass parent_class;
};

gboolean
fu_cab_firmware_get_compressed(FuCabFirmware *self);
void
fu_cab_firmware_set_compressed(FuCabFirmware *self, gboolean compressed);
gboolean
fu_cab_firmware_get_only_basename(FuCabFirmware *self);
void
fu_cab_firmware_set_only_basename(FuCabFirmware *self, gboolean only_basename);

FuCabFirmware *
fu_cab_firmware_new(void) G_GNUC_WARN_UNUSED_RESULT;
