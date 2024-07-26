/*
 * Copyright 2024 Dell Technologies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#include "config.h"

#include "fu-dell-dock2-common.h"

gchar *
fu_hex_version_from_uint32(guint32 val, FwupdVersionFormat kind)
{
	if (kind == FWUPD_VERSION_FORMAT_QUAD) {
		/* AA.BB.CC.DD */
		return g_strdup_printf("%x.%x.%x.%x",
				       (val >> 24) & 0xff,
				       (val >> 16) & 0xff,
				       (val >> 8) & 0xff,
				       val & 0xff);
	}
	if (kind == FWUPD_VERSION_FORMAT_PAIR) {
		/* AABB.CCDD */
		return g_strdup_printf("%x.%x", (val >> 16) & 0xffff, val & 0xffff);
	}
	return NULL;
}
