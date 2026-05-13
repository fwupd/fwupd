/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * nocheck:expect: do not use goto
 */

static void
fu_blocked_goto_test(void)
{
	for (guint i = 0; i < 10; i++) {
		if (i == 5)
			goto end;
	}
end:
	return;
}
