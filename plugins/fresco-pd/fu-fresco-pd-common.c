/*
 * Copyright (C) 2020 Fresco Logic
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-fresco-pd-common.h"

gchar *
fu_fresco_pd_version_from_buf (const guint8 ver[4])
{
	if (ver[3] == 1 || ver[3] == 2)
		return g_strdup_printf ("%u.%u.%u.%u", ver[0], ver[1], ver[2], ver[3]);
	return g_strdup_printf ("%u.%u.%u.%u", ver[3], ver[1], ver[2], ver[0]);
}
