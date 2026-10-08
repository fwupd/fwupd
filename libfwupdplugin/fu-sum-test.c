/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

static void
fu_sum8_func(void)
{
	gboolean ret;
	guint8 value = 0;
	g_autoptr(GError) error = NULL;
	const guint8 buf[] = {0x01, 0x02, 0x03, 0xff};
	g_autoptr(GBytes) blob = g_bytes_new_static(buf, sizeof(buf));
	g_autoptr(GBytes) blob_empty = g_bytes_new_static(NULL, 0);

	/* 0x01 + 0x02 + 0x03 + 0xff == 0x105, truncated to 0x05 */
	g_assert_cmpint(fu_sum8(buf, sizeof(buf)), ==, 0x05);
	g_assert_cmpint(fu_sum8_bytes(blob), ==, 0x05);
	g_assert_cmpint(fu_sum8_bytes(blob_empty), ==, 0x00);

	/* safe variant, in-bounds */
	ret = fu_sum8_safe(buf, sizeof(buf), 0x1, 0x2, &value, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(value, ==, 0x05);

	/* safe variant, out-of-bounds */
	ret = fu_sum8_safe(buf, sizeof(buf), 0x2, sizeof(buf), NULL, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_READ);
	g_assert_false(ret);
}

static void
fu_sum16_func(void)
{
	gboolean ret;
	guint16 value = 0;
	g_autoptr(GError) error = NULL;
	const guint8 buf[] = {0x01, 0x02, 0x03, 0x04};
	g_autoptr(GBytes) blob = g_bytes_new_static(buf, sizeof(buf));

	/* summed one byte at a time */
	g_assert_cmpint(fu_sum16(buf, sizeof(buf)), ==, 0x0a);
	g_assert_cmpint(fu_sum16_bytes(blob), ==, 0x0a);

	/* summed one word at a time */
	g_assert_cmpint(fu_sum16w(buf, sizeof(buf), G_LITTLE_ENDIAN), ==, 0x0604);
	g_assert_cmpint(fu_sum16w(buf, sizeof(buf), G_BIG_ENDIAN), ==, 0x0406);
	g_assert_cmpint(fu_sum16w_bytes(blob, G_LITTLE_ENDIAN), ==, 0x0604);

	/* safe variant, in-bounds */
	ret = fu_sum16_safe(buf, sizeof(buf), 0x1, 0x2, &value, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(value, ==, 0x05);

	/* safe variant, out-of-bounds */
	ret = fu_sum16_safe(buf, sizeof(buf), 0x2, sizeof(buf), NULL, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_READ);
	g_assert_false(ret);
}

static void
fu_sum32_func(void)
{
	const guint8 buf[] = {0x01, 0x02, 0x03, 0x04};
	g_autoptr(GBytes) blob = g_bytes_new_static(buf, sizeof(buf));

	/* summed one byte at a time */
	g_assert_cmpint(fu_sum32(buf, sizeof(buf)), ==, 0x0a);
	g_assert_cmpint(fu_sum32_bytes(blob), ==, 0x0a);

	/* summed one dword at a time */
	g_assert_cmpint(fu_sum32w(buf, sizeof(buf), G_LITTLE_ENDIAN), ==, 0x04030201);
	g_assert_cmpint(fu_sum32w(buf, sizeof(buf), G_BIG_ENDIAN), ==, 0x01020304);
	g_assert_cmpint(fu_sum32w_bytes(blob, G_LITTLE_ENDIAN), ==, 0x04030201);
	g_assert_cmpint(fu_sum32w_bytes(blob, G_BIG_ENDIAN), ==, 0x01020304);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/sum8", fu_sum8_func);
	g_test_add_func("/fwupd/sum16", fu_sum16_func);
	g_test_add_func("/fwupd/sum32", fu_sum32_func);
	return g_test_run();
}
