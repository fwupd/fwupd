/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-firmware.h"
#include "fu-zip-struct.h"

G_BEGIN_DECLS

#define FU_TYPE_ZIP_FILE (fu_zip_file_get_type())

G_DECLARE_DERIVABLE_TYPE(FuZipFile, fu_zip_file, FU, ZIP_FILE, FuFirmware)

struct _FuZipFileClass {
	FuFirmwareClass parent_class;
};

FuZipCompression
fu_zip_file_get_compression(FuZipFile *self) G_GNUC_NON_NULL(1) G_GNUC_PURE;
void
fu_zip_file_set_compression(FuZipFile *self, FuZipCompression compression) G_GNUC_NON_NULL(1);

FuFirmware *
fu_zip_file_new(void) G_GNUC_WARN_UNUSED_RESULT;

G_END_DECLS
