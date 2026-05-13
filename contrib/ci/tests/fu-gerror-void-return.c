/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * nocheck:expect: void return type not expected for GError
 */

static void
fu_gerror_void_return(FuCustomDevice *self, GError **error)
{
	return;
}
