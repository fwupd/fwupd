/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <gio/gio.h>

/**
 * FuIfdRegion:
 * @FU_IFD_REGION_DESC:		IFD descriptor
 * @FU_IFD_REGION_BIOS:		BIOS
 * @FU_IFD_REGION_ME:		ME
 * @FU_IFD_REGION_GBE:		GBE
 * @FU_IFD_REGION_PLATFORM:	Platform
 * @FU_IFD_REGION_DEVEXP:	Developer
 * @FU_IFD_REGION_BIOS2:	BIOS Backup
 * @FU_IFD_REGION_EC:		Embedded Controller
 * @FU_IFD_REGION_IE:		IE
 * @FU_IFD_REGION_10GBE:	10GBE
 * @FU_IFD_REGION_MAX:		Maximum value
 *
 * The IFD region.
 **/
typedef enum {
	FU_IFD_REGION_DESC = 0x00,
	FU_IFD_REGION_BIOS = 0x01,
	FU_IFD_REGION_ME = 0x02,
	FU_IFD_REGION_GBE = 0x03,
	FU_IFD_REGION_PLATFORM = 0x04,
	FU_IFD_REGION_DEVEXP = 0x05,
	FU_IFD_REGION_BIOS2 = 0x06,
	FU_IFD_REGION_EC = 0x08,
	FU_IFD_REGION_IE = 0x0A,
	FU_IFD_REGION_10GBE = 0x0B,
	FU_IFD_REGION_MAX = 0x0F,
} FuIfdRegion;

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

#define FU_IFD_FREG_BASE(freg)	(((freg) << 12) & 0x07FFF000)
#define FU_IFD_FREG_LIMIT(freg) ((((freg) >> 4) & 0x07FFF000) | 0x00000FFF)

const gchar *
fu_ifd_region_to_string(FuIfdRegion region);
const gchar *
fu_ifd_region_to_name(FuIfdRegion region);
const gchar *
fu_ifd_access_to_string(FuIfdAccess access);

FuIfdAccess
fu_ifd_region_to_access(FuIfdRegion region, guint32 flash_master, gboolean new_layout);
