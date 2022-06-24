/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2022 Intel
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-firmware.h"

#define FU_TYPE_OPROM_FIRMWARE (fu_oprom_firmware_get_type())
G_DECLARE_DERIVABLE_TYPE(FuOpromFirmware, fu_oprom_firmware, FU, OPROM_FIRMWARE, FuFirmware)

struct _FuOpromFirmwareClass {
	FuFirmwareClass parent_class;
};

/**
 * FU_OPROM_FIRMWARE_COMPRESSION_TYPE_NONE:
 *
 * No compression.
 *
 * Since: 1.8.2
 **/
#define FU_OPROM_FIRMWARE_COMPRESSION_TYPE_NONE 0x00

/**
 * FU_OPROM_FIRMWARE_SUBSYSTEM_EFI_BOOT_SRV_DRV:
 *
 * EFI boot.
 *
 * Since: 1.8.2
 **/
#define FU_OPROM_FIRMWARE_SUBSYSTEM_EFI_BOOT_SRV_DRV 0x00

/**
 * FU_OPROM_FIRMWARE_MACHINE_TYPE_X64:
 *
 * AMD64 machine type.
 *
 * Since: 1.8.2
 **/
#define FU_OPROM_FIRMWARE_MACHINE_TYPE_X64 0x00

FuFirmware *
fu_oprom_firmware_new(void);

guint16
fu_oprom_firmware_get_machine_type(FuOpromFirmware *self);
guint16
fu_oprom_firmware_get_subsystem(FuOpromFirmware *self);
guint16
fu_oprom_firmware_get_compression_type(FuOpromFirmware *self);
