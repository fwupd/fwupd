/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * nocheck:expect: variable has too many magic values
 */

static void
fu_magic_numbers_buffer(void)
{
	guint8 start_cmd[] = {FOO, 0x75, 0x65, 0x55, 0x45, 0x63, 0x75, 0x69, 0x33};
}
