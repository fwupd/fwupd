/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2022 Gaël PORTAY <gael.portay@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-archive.h"
#include "fu-firmware.h"

#define FU_TYPE_ARCHIVE_FIRMWARE	(fu_archive_firmware_get_type())
#define FU_TYPE_ARCHIVE_FIRMWARE_RECORD (fu_archive_firmware_record_get_type())
G_DECLARE_DERIVABLE_TYPE(FuArchiveFirmware, fu_archive_firmware, FU, ARCHIVE_FIRMWARE, FuFirmware)

struct _FuArchiveFirmwareClass {
	FuFirmwareClass parent_class;
};

FuFirmware *
fu_archive_firmware_new(void);
FuArchiveFormat
fu_archive_firmware_get_format(FuArchiveFirmware *self);
void
fu_archive_firmware_set_format(FuArchiveFirmware *self, FuArchiveFormat format);
FuArchiveCompression
fu_archive_firmware_get_compression(FuArchiveFirmware *self);
void
fu_archive_firmware_set_compression(FuArchiveFirmware *self, FuArchiveCompression compression);
