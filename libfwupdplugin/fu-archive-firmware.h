/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-firmware.h"

#define FU_TYPE_ARCHIVE_FIRMWARE	(fu_archive_firmware_get_type())
#define FU_TYPE_ARCHIVE_FIRMWARE_RECORD (fu_archive_firmware_record_get_type())
G_DECLARE_DERIVABLE_TYPE(FuArchiveFirmware, fu_archive_firmware, FU, ARCHIVE_FIRMWARE, FuFirmware)

struct _FuArchiveFirmwareClass {
	FuFirmwareClass parent_class;
};

FuFirmware *
fu_archive_firmware_new(void);
