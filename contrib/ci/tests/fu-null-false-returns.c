/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * nocheck:expect: g_return_val_if_fail() return type invalid
 * nocheck:expect: return type invalid for gboolean
 */

static gboolean
fu_null_false_returns(gpointer foo)
{
	g_return_val_if_fail(foo != NULL, NULL);
	if (1) {
		return G_MAXUINT32;
	}
	return NULL;
}
