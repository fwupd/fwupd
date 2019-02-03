/*
 * Copyright (C) 2017-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN				"FuCommon"

#include <config.h>

#include "fu-common-guid.h"

/**
 * fu_common_guid_is_plausible:
 * @buf: a buffer of data
 *
 * Checks whether a chunk of memory looks like it could be a GUID.
 *
 * Returns: TRUE if it looks like a GUID, FALSE if not
 *
 * Since: 1.2.5
 **/
gboolean
fu_common_guid_is_plausible (const guint8 *buf)
{
	guint guint_sum = 0;

	for (guint i = 0; i < 16; i++)
		guint_sum += buf[i];
	if (guint_sum == 0x00)
		return FALSE;
	if (guint_sum < 0xff)
		return FALSE;
	return TRUE;
}
