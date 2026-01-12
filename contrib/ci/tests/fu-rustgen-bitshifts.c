/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * nocheck:expect: endian unsafe construction
 */

static guint32
fu_rustgen_bitshifts(void)
{
	return buf[13] << 24 | buf[14] << 16 | buf[15] << 8 | buf[16];
}
