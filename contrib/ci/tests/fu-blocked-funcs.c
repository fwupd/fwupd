/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * nocheck:expect: contains blocked token g_ascii_strtoull
 */

static void
fu_blocked_funcs_test(void)
{
	guint32 i = g_ascii_strtoull(n);
}
