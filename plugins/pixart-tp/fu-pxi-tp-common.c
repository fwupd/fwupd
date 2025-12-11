/*
 * Copyright 2025 Harris Tai <harris_tai@pixart.com>
 * Copyright 2025 Micky Hsieh <micky_hsieh@pixart.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <glib/gprintf.h>

#include "fu-pxi-tp-common.h"

gchar *
fu_pxi_tp_common_hexdump_slice(const guint8 *p, gsize n, gsize maxbytes)
{
	gsize m = n < maxbytes ? n : maxbytes;
	GString *g;

	/* nothing to dump */
	if (m == 0)
		return g_strdup("");

	/* reserve "XX " * m bytes */
	g = g_string_sized_new(m * 3);

	for (gsize i = 0; i < m; i++)
		g_string_append_printf(g, "%02X%s", p[i], (i + 1 == m) ? "" : " ");

	/* return as normal gchar* */
	return g_string_free(g, FALSE);
}
