/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * nocheck:expect: uses g_set_error() without using FWUPD_ERROR
 */

static gboolean
fu_gerror_domain_test(GError **error)
{
	g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "foo");
	return FALSE;
}
