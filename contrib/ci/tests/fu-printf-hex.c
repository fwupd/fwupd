/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * nocheck:expect: printf format should use 0x%x rather than %#x
 */

static void
fu_printf_hex(guint32 address, gsize bufsz)
{
	/* fine */
	g_debug("read 0x%zx bytes from 0x%06x", bufsz, address);
	g_debug("literal %%#x is fine");

	/* wrong */
	g_debug("read %#zx bytes from %#08x", bufsz, address);
}
