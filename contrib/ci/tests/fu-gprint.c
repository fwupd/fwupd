/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * nocheck:expect: contains blocked token g_printerr
 */

static void
fu_gprint(void)
{
	g_printerr("fixme");
}
