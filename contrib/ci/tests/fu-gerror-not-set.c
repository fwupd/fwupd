/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * nocheck:expect: uses g_prefix_error() without setting GError
 */

static gboolean
fu_gerror_not_set_test(void)
{
	g_autoptr(GError) error = NULL;
	if (0) {
		g_prefix_error_literal(error, "test: ");
	}
}
