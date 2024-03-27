/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <gio/gio.h>

#include "fu-ifd-struct.h"

/**
 * FuIfdAccess:
 * @FU_IFD_ACCESS_NONE:		None
 * @FU_IFD_ACCESS_READ:		Readable
 * @FU_IFD_ACCESS_WRITE:	Writable
 *
 * The flags to use for IFD access permissions.
 **/
typedef enum {
	FU_IFD_ACCESS_NONE = 0,
	FU_IFD_ACCESS_READ = 1 << 0,
	FU_IFD_ACCESS_WRITE = 1 << 1,
} FuIfdAccess;

const gchar *
fu_ifd_region_to_name(FuIfdRegion region);
const gchar *
fu_ifd_access_to_string(FuIfdAccess access);

FuIfdAccess
fu_ifd_region_to_access(FuIfdRegion region, guint32 flash_master, gboolean new_layout);
