/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * nocheck:expect: contains blocked token g_print
 * nocheck:expect: g_info should not contain newlines
 * nocheck:expect: g_debug should not end with a full stop
 * nocheck:expect: g_debug should not use sentence case
 * nocheck:expect: single line comments should not use sentence case
 */

static void
fu_gprint(void)
{
	/*
	 * This is a start of a long story,
	 * so we're allowing proper prose.
	 */
	g_printerr("fixme");
	/* But this IS wrong */
	g_debug("This is wrong.");
	g_info("fixme\n");
}
