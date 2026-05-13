/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * nocheck:expect: Use FU_BIT_SET() instead
 * nocheck:expect: Use FU_BIT_CLEAR() instead
 */

static void
fu_blocked_bitset_test(void)
{
	guint32 i = 0;
	i |= 1u << 6;
	i &= ~(1u << 8);
}
