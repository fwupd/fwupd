/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * nocheck:expect: dereferences GError; use error_local instead
 */

static gboolean
fu_gerror_deref(void)
{
	g_debug("%s", (*error)->message);
}
