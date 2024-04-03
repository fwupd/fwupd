/*
 * Copyright 2024 Randy Lai <randy.lai@weidahitech.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-weida-raw-common.h"

gboolean
fu_weida_raw_block_is_empty(const guint8 *data, gsize datasz)
{
	for (gsize i = 0; i < datasz; i++) {
		if (data[i] != 0xFF)
			return FALSE;
	}
	return TRUE;
}
