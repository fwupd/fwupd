/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * nocheck:expect: use C style comments
 * nocheck:expect: do not use boxed comment lines
 */

static void
fu_comments(void)
{
	g_debug("hello");
	// c++ comments
	g_debug("world");
	/**** not boxes *****/
}
