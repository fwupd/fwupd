/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * nocheck:expect: is nested too deep
 */

static gboolean
fu_nesting_depth_test(void)
{
	if (1) {
		if (2) {
			if (3) {
				if (4) {
					if (5) {
						/* this is crazy */
						g_debug("foo");
					}
				}
			}
		}
	}
}
