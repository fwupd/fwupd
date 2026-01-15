/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-firmware.h"

#define FU_TYPE_ZIP_ARCHIVE (fu_zip_archive_get_type())

G_DECLARE_DERIVABLE_TYPE(FuZipArchive, fu_zip_archive, FU, ZIP_ARCHIVE, FuFirmware)

struct _FuZipArchiveClass {
	FuFirmwareClass parent_class;
};

FuZipArchive *
fu_zip_archive_new(void) G_GNUC_WARN_UNUSED_RESULT;
