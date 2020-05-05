/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <gio/gio.h>

#define FU_TYPE_UEFI_DBX_FILE (fu_uefi_dbx_file_get_type ())
G_DECLARE_FINAL_TYPE (FuUefiDbxFile, fu_uefi_dbx_file, FU, UEFI_DBX_FILE, GObject)


typedef enum {
	FU_UEFI_DBX_FILE_PARSE_FLAGS_NONE		= 0,
	FU_UEFI_DBX_FILE_PARSE_FLAGS_IGNORE_HEADER	= 1 << 0,
} FuUefiDbxFileParseFlags;

FuUefiDbxFile	*fu_uefi_dbx_file_new		(const guint8	*buf,
						 gsize		 bufsz,
						 FuUefiDbxFileParseFlags flags,
						 GError		**error);
GPtrArray	*fu_uefi_dbx_file_get_checksums	(FuUefiDbxFile	*self);
gboolean	 fu_uefi_dbx_file_has_checksum	(FuUefiDbxFile	*self,
						 const gchar	*checksum);
