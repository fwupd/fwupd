/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-ti-tps6598x-common.h"

static void
fu_ti_tps6598x_common_nonzero_func(void)
{
	/* only bytes *after* the first are checked */
	const guint8 empty[] = {0x0};
	const guint8 zero[] = {0x0, 0x0, 0x0};
	const guint8 first_only[] = {0xff, 0x0, 0x0};
	const guint8 nonzero[] = {0x0, 0x0, 0xff};
	g_autoptr(GByteArray) buf_empty = g_byte_array_new();
	g_autoptr(GByteArray) buf0 = g_byte_array_new();
	g_autoptr(GByteArray) buf_zero = g_byte_array_new();
	g_autoptr(GByteArray) buf_first = g_byte_array_new();
	g_autoptr(GByteArray) buf_nonzero = g_byte_array_new();

	g_assert_false(fu_ti_tps6598x_byte_array_is_nonzero(buf_empty));

	g_byte_array_append(buf0, empty, sizeof(empty));
	g_assert_false(fu_ti_tps6598x_byte_array_is_nonzero(buf0));

	g_byte_array_append(buf_zero, zero, sizeof(zero));
	g_assert_false(fu_ti_tps6598x_byte_array_is_nonzero(buf_zero));

	g_byte_array_append(buf_first, first_only, sizeof(first_only));
	g_assert_false(fu_ti_tps6598x_byte_array_is_nonzero(buf_first));

	g_byte_array_append(buf_nonzero, nonzero, sizeof(nonzero));
	g_assert_true(fu_ti_tps6598x_byte_array_is_nonzero(buf_nonzero));
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/ti-tps6598x/common{nonzero}", fu_ti_tps6598x_common_nonzero_func);
	return g_test_run();
}
