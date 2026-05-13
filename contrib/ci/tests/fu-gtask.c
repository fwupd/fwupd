/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * nocheck:expect: missing literal, use g_task_return_new_error_literal
 */

static void
fu_gtask_test(void)
{
	g_task_return_new_error(task, domain, code, "abc");
}
