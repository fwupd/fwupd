/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <gio/gio.h>

typedef enum {
	FU_IFD_REGION_DESC			= 0x00,
	FU_IFD_REGION_BIOS			= 0x01,
	FU_IFD_REGION_ME			= 0x02,
	FU_IFD_REGION_GBE			= 0x03,
	FU_IFD_REGION_PLATFORM			= 0x04,
	FU_IFD_REGION_DEVEXP			= 0x05,
	FU_IFD_REGION_BIOS2			= 0x06,
	FU_IFD_REGION_EC			= 0x08,
	FU_IFD_REGION_IE			= 0x0A,
	FU_IFD_REGION_10GBE			= 0x0B,
	FU_IFD_REGION_MAX			= 0x0F,
} FuIfdRegion;

typedef enum {
	FU_IFD_ACCESS_NONE			= 0,
	FU_IFD_ACCESS_READ			= 1 << 0,
	FU_IFD_ACCESS_WRITE			= 1 << 1,
} FuIfdAccess;

const gchar	*fu_ifd_region_to_string	(FuIfdRegion	 region);
const gchar	*fu_ifd_access_to_string	(FuIfdAccess	 access);
