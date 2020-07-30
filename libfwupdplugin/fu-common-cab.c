/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN				"FuCommonCab"

#include "config.h"

#include "fu-cabinet.h"
#include "fu-common-cab.h"

/**
 * fu_common_cab_build_silo: (skip):
 * @blob: A readable blob
 * @size_max: The maximum size of the archive
 * @error: A #FuEndianType, e.g. %G_LITTLE_ENDIAN
 *
 * Create an AppStream silo from a cabinet archive.
 *
 * Returns: a #XbSilo, or %NULL on error
 *
 * Since: 1.2.0
 **/
XbSilo *
fu_common_cab_build_silo (GBytes *blob, guint64 size_max, GError **error)
{
	g_autoptr(FuCabinet) cabinet = fu_cabinet_new ();
	fu_cabinet_set_size_max (cabinet, size_max);
	if (!fu_cabinet_parse (cabinet, blob, FU_CABINET_PARSE_FLAG_NONE, error))
		return NULL;
	return fu_cabinet_get_silo (cabinet);
}
