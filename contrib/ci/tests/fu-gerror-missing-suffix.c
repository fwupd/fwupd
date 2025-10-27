/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * nocheck:expect: missing ': ' suffix
 */

static void
fu_gerror_missing_suffix_test(void)
{
	if (!foo_cb(self, &error)) {
		g_prefix_error_literal(error, "foo");
		return FALSE;
	}
}
