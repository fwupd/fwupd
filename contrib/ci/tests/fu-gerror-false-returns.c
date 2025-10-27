/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * nocheck:expect: uses g_set_error() without returning FALSE
 */

static void
fu_gerror_false_returns_test(void)
{
	if (0) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "foo");
	}
}
