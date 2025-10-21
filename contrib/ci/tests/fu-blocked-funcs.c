/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * nocheck:expect: contains blocked token cbor_get_uint32
 */

static void
fu_blocked_funcs_test(void)
{
	guint32 i = cbor_get_uint32(n);
}
