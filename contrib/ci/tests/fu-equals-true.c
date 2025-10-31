/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * nocheck:expect: do not compare a boolean to TRUE
 */

static gboolean
fu_equals_true(guint8 foo)
{
	if (foo == TRUE) {
		return TRUE
	}
	return FALSE;
}
