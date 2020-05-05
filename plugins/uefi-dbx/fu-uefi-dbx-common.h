/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <gio/gio.h>

#define FU_UEFI_DBX_DATA_URL	"https://uefi.org/revocationlistfile"

gchar		*fu_uefi_dbx_get_dbxupdate	(GError		**error);
