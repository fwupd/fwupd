/*
 * Copyright 2025 Joe Hong <joe_hung@ilitek.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-ilitek-its-common.h"

gchar *
fu_ilitek_its_convert_version(guint64 version_raw)
{
	/*
	 * convert 8 byte version in to human readable format.
	 * e.g. convert 0x0700000101020304 into 700.1.102.304
	 */
	return g_strdup_printf("%x.%x.%x.%x",
			       (guint)((version_raw >> 48) & 0xFFFF),
			       (guint)((version_raw >> 32) & 0xFFFF),
			       (guint)((version_raw >> 16) & 0xFFFF),
			       (guint)(version_raw & 0xFFFF));
}
