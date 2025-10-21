/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * nocheck:expect: has too many switches
 */

static void
fu_function_length_switch_test(void)
{
	switch (foo) {
	case 1:
		break;
	default:
		break;
	}
	switch (bar) {
	case 1:
		break;
	default:
		break;
	}
	switch (baz) {
	case 1:
		break;
	default:
		break;
	}
}
