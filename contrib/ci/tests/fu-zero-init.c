/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * nocheck:expect: buffer not zero init
 */

static gboolean
fu_zero_init_test(void)
{
	guint8 buf[10];
}
