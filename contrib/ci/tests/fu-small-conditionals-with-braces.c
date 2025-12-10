/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * nocheck:expect: no {} required for small single-line conditional
 */

static gboolean
fu_small_conditionals_with_braces(guint8 foo)
{
	if (TRUE) {
		g_debug("this");
		g_debug("is fine");
	}
	if (TRUE) {
		g_debug("here");
	}
	if (TRUE) {
		g_debug("this is fine too,");
	} else {
		g_debug("because of this");
	}
	if (fu_this_that_and_something_else(foo) == FU_LONG_DEFINE1 ||
	    fu_this_that_and_something_else(foo) == FU_LONG_DEFINE4) {
		g_debug("here");
	}
	for (guint i = 0; i < 10; i++) {
		if (foo)
			g_debug("fine");
	}
}
