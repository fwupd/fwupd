/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * nocheck:expect: uses g_set_error() without returning a value
 */

static gboolean
fu_gerror_no_return_test(void)
{
	g_autoptr(GError) error = NULL;
	if (!foo_cb(self, error)) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "test");
	}
}
