/*
 * Copyright (C) 2018-2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib.h>

typedef enum {
	FU_UEFI_DEVPATH_PARSE_FLAG_NONE		= 0,
	FU_UEFI_DEVPATH_PARSE_FLAG_REPAIR	= 1 << 0,
	FU_UEFI_DEVPATH_PARSE_FLAG_LAST
} FuUefiDevpathParseFlags;

GPtrArray	*fu_uefi_devpath_parse		(const guint8	*buf,
						 gsize		 sz,
						 FuUefiDevpathParseFlags flags,
						 GError		**error);
GBytes		*fu_uefi_devpath_find_data	(GPtrArray	*dps,
						 guint8		 type,
						 guint8		 subtype,
						 GError		**error);
