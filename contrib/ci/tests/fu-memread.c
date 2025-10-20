/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * nocheck:expect: Too many calls to fu_memread_uintXX
 */

static void
fu_memread_func(void)
{
	guint16 tmp;
	guint64 total;

	tmp = fu_memread_uint16(buf + 0x0, G_LITTLE_ENDIAN);
	total += tmp;
	tmp = fu_memread_uint16(buf + 0x2, G_LITTLE_ENDIAN);
	total += tmp;
	tmp = fu_memread_uint16(buf + 0x4, G_LITTLE_ENDIAN);
	total += tmp;
	tmp = fu_memread_uint16(buf + 0x6, G_LITTLE_ENDIAN);
	total += tmp;
	tmp = fu_memread_uint16(buf + 0x8, G_LITTLE_ENDIAN);
	total += tmp;
	tmp = fu_memread_uint16(buf + 0xA, G_LITTLE_ENDIAN);
	total += tmp;
	tmp = fu_memread_uint16(buf + 0xC, G_LITTLE_ENDIAN);
	total += tmp;
	tmp = fu_memread_uint16(buf + 0xE, G_LITTLE_ENDIAN);
}
