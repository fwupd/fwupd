/*
 * Copyright 2022 Richard Hughes <richard@hughsie.com>
 * Copyright 2022 Intel
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-firmware.h"
#include "fu-oprom-struct.h"

#define FU_TYPE_OPROM_FIRMWARE (fu_oprom_firmware_get_type())
G_DECLARE_DERIVABLE_TYPE(FuOpromFirmware, fu_oprom_firmware, FU, OPROM_FIRMWARE, FuFirmware)

struct _FuOpromFirmwareClass {
	FuFirmwareClass parent_class;
};

FuFirmware *
fu_oprom_firmware_new(void);

FuOpromMachineType
fu_oprom_firmware_get_machine_type(FuOpromFirmware *self) G_GNUC_NON_NULL(1);
FuOpromSubsystem
fu_oprom_firmware_get_subsystem(FuOpromFirmware *self) G_GNUC_NON_NULL(1);
FuOpromCompressionType
fu_oprom_firmware_get_compression_type(FuOpromFirmware *self) G_GNUC_NON_NULL(1);
