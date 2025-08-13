/*
 * Copyright 2025 Joe hong <JoeHung@ilitek.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-ilitek-its-common.h"

const gchar *
fu_ilitek_its_strerror(guint8 code)
{
	if (code == 0)
		return "success";
	return NULL;
}
