/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * nocheck:expect: missing literal, use g_set_error_literal() instead
 */

static void
fu_gerror_literal_test(void)
{
	g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "test");
	return FALSE;
}
