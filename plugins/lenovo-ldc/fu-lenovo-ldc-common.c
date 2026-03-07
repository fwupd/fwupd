/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-lenovo-ldc-common.h"

const gchar *
fu_lenovo_ldc_strerror(guint8 code)
{
	if (code == 0)
		return "success";
	return NULL;
}
