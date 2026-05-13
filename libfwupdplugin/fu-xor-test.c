/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

static void
fu_xor8_func(void)
{
	guint8 buf[] = {0x12, 0x23, 0x45, 0x67, 0x89};
	g_assert_cmpint(fu_xor8(buf, sizeof(buf)), ==, 0x9A);
	g_assert_cmpint(fu_xor8(buf, 0), ==, 0);
}

static void
fu_xor8_safe_func(void)
{
	guint8 checksum = 0x0;
	gboolean ret;
	guint8 buf[] = {0x12, 0x23, 0x45, 0x67, 0x89};
	g_autoptr(GError) error = NULL;

	ret = fu_xor8_safe(buf, sizeof(buf), 0x0, 5, &checksum, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(checksum, ==, 0x9A);
	ret = fu_xor8_safe(buf, sizeof(buf), 0x0, 5, &checksum, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(checksum, ==, 0x00);

	ret = fu_xor8_safe(buf, sizeof(buf), 0x33, 0x999, &checksum, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_READ);
	g_assert_false(ret);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/xor8", fu_xor8_func);
	g_test_add_func("/fwupd/xor8/safe", fu_xor8_safe_func);
	return g_test_run();
}
