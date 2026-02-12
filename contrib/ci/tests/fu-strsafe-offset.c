/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * nocheck:expect: use fu_memstrsafe() rather than fu_strsafe() if reading with an offset
 */

static void
fu_strsafe_offset(void)
{
	str = fu_strsafe((const gchar *)buf->data + offset, maxsz);
	str = fu_strsafe((const gchar *)buf + offset, maxsz);
	str = fu_strsafe((const char *)buf + offset, maxsz);
	str = fu_strsafe((gchar *)buf + offset, maxsz);
}
