/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-test.h"

static void
fu_byte_array_func(void)
{
	g_autofree gchar *str = NULL;
	g_autoptr(GByteArray) array = g_byte_array_new();
	g_autoptr(GByteArray) array2 = NULL;
	g_autoptr(GByteArray) array3 = NULL;
	g_autoptr(GError) error = NULL;

	fu_byte_array_append_uint8(array, (guint8)'h');
	fu_byte_array_append_uint8(array, (guint8)'e');
	fu_byte_array_append_uint8(array, (guint8)'l');
	fu_byte_array_append_uint8(array, (guint8)'l');
	fu_byte_array_append_uint8(array, (guint8)'o');
	g_assert_cmpint(array->len, ==, 5);
	g_assert_cmpint(memcmp(array->data, "hello", array->len), ==, 0);

	fu_byte_array_set_size(array, 10, 0x00);
	g_assert_cmpint(array->len, ==, 10);
	g_assert_cmpint(memcmp(array->data, "hello\0\0\0\0\0", array->len), ==, 0);

	str = fu_byte_array_to_string(array);
	g_assert_cmpstr(str, ==, "68656c6c6f0000000000");

	array2 = fu_byte_array_from_string(str, &error);
	g_assert_no_error(error);
	g_assert_nonnull(array2);
	g_assert_cmpint(array2->len, ==, 10);
	g_assert_cmpint(memcmp(array2->data, "hello\0\0\0\0\0", array2->len), ==, 0);

	array3 = fu_byte_array_from_string("ZZZ", &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_null(array3);
}

static void
fu_byte_array_safe_func(void)
{
	gboolean ret;
	g_autoptr(GError) error = NULL;
	g_autoptr(GByteArray) array = g_byte_array_new();
	const guint8 buf[] = {0x01, 0x02, 0x03};

	/* all buffer */
	ret = fu_byte_array_append_safe(array, buf, sizeof(buf), 0, sizeof(buf), &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(array->len, ==, 3);

	/* +1, -1 */
	ret = fu_byte_array_append_safe(array, buf, sizeof(buf), 1, sizeof(buf) - 1, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(array->len, ==, 3 + 2);

	/* boom */
	ret = fu_byte_array_append_safe(array, buf, sizeof(buf), 1, sizeof(buf), &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_READ);
	g_assert_false(ret);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/byte-array", fu_byte_array_func);
	g_test_add_func("/fwupd/byte-array/safe", fu_byte_array_safe_func);
	return g_test_run();
}
