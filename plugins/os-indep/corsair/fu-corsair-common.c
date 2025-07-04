/*
 * Copyright 2022 Andrii Dushko <andrii.dushko@developex.net>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-corsair-common.h"

/**
 * fu_corsair_version_from_uint32:
 * @val: version in corsair device format
 *
 * fu_version_from_uint32(... %FWUPD_VERSION_FORMAT_TRIPLET)
 * cannot be used because bytes in the version are in non-standard
 * order: 0xCCDD.BB.AA.
 *
 * Returns: a version number, e.g. `1.0.3`.
 **/
gchar *
fu_corsair_version_from_uint32(guint32 value)
{
	return g_strdup_printf("%u.%u.%u",
			       value & 0xff,
			       (value >> 8) & 0xff,
			       (value >> 16) & 0xffff);
}
