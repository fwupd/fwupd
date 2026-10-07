/*
 * Copyright 2026 NVIDIA Corporation
 * Author: Vishnu Raghav <vraghav@nvidia.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-realtek-alc408x-common.h"

/* the version is stored as two u32 halves, e.g. 0x10020003 and 0x00040005 are shown as
 * 1.002.0003-0004.0005; every field is fixed width so the string sorts the same way as the
 * raw value */
gchar *
fu_realtek_alc408x_version_to_string(guint64 version_raw)
{
	guint32 hi = version_raw >> 32;
	guint32 lo = version_raw & G_MAXUINT32;
	return g_strdup_printf("%X.%03X.%04X-%04X.%04X",
			       hi >> 28,
			       (hi >> 16) & 0xFFF,
			       hi & 0xFFFF,
			       lo >> 16,
			       lo & 0xFFFF);
}
