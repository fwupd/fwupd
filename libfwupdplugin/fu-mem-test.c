/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-mem.h"
#include "fu-test.h"

static void
fu_mem_memmem_func(void)
{
	const guint8 haystack[] = {'H', 'A', 'Y', 'S'};
	const guint8 needle[] = {'A', 'Y'};
	gboolean ret;
	gsize offset = 0;
	g_autoptr(GError) error = NULL;

	ret = fu_memmem_safe(haystack, sizeof(haystack), needle, sizeof(needle), &offset, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(offset, ==, 0x1);

	ret = fu_memmem_safe(haystack + 2,
			     sizeof(haystack) - 2,
			     needle,
			     sizeof(needle),
			     &offset,
			     &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_false(ret);
}

static void
fu_mem_endian_func(void)
{
	guint8 buf[3] = {0};

	fu_memwrite_uint16(buf, 0x1234, G_LITTLE_ENDIAN);
	g_assert_cmpint(buf[0], ==, 0x34);
	g_assert_cmpint(buf[1], ==, 0x12);
	g_assert_cmpint(fu_memread_uint16(buf, G_LITTLE_ENDIAN), ==, 0x1234);

	fu_memwrite_uint16(buf, 0x1234, G_BIG_ENDIAN);
	g_assert_cmpint(buf[0], ==, 0x12);
	g_assert_cmpint(buf[1], ==, 0x34);
	g_assert_cmpint(fu_memread_uint16(buf, G_BIG_ENDIAN), ==, 0x1234);

	fu_memwrite_uint24(buf, 0x123456, G_LITTLE_ENDIAN);
	g_assert_cmpint(buf[0], ==, 0x56);
	g_assert_cmpint(buf[1], ==, 0x34);
	g_assert_cmpint(buf[2], ==, 0x12);
	g_assert_cmpint(fu_memread_uint24(buf, G_LITTLE_ENDIAN), ==, 0x123456);

	fu_memwrite_uint24(buf, 0x123456, G_BIG_ENDIAN);
	g_assert_cmpint(buf[0], ==, 0x12);
	g_assert_cmpint(buf[1], ==, 0x34);
	g_assert_cmpint(buf[2], ==, 0x56);
	g_assert_cmpint(fu_memread_uint24(buf, G_BIG_ENDIAN), ==, 0x123456);
}

static void
fu_mem_memcpy_func(void)
{
	const guint8 src[] = {'a', 'b', 'c', 'd', 'e'};
	gboolean ret;
	guint8 dst[4] = {0};
	g_autoptr(GError) error = NULL;

	/* copy entire buffer */
	ret = fu_memcpy_safe(dst, sizeof(dst), 0x0, src, sizeof(src), 0x0, 4, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(memcmp(src, dst, 4), ==, 0);

	/* copy first char */
	ret = fu_memcpy_safe(dst, sizeof(dst), 0x0, src, sizeof(src), 0x0, 1, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(dst[0], ==, 'a');

	/* copy last char */
	ret = fu_memcpy_safe(dst, sizeof(dst), 0x0, src, sizeof(src), 0x4, 1, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(dst[0], ==, 'e');

	/* copy nothing */
	ret = fu_memcpy_safe(dst, sizeof(dst), 0x0, src, sizeof(src), 0x0, 0, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* write past the end of dst */
	ret = fu_memcpy_safe(dst, sizeof(dst), 0x0, src, sizeof(src), 0x0, 5, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_WRITE);
	g_assert_false(ret);
	g_clear_error(&error);

	/* write past the end of dst with offset */
	ret = fu_memcpy_safe(dst, sizeof(dst), 0x1, src, sizeof(src), 0x0, 4, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_WRITE);
	g_assert_false(ret);
	g_clear_error(&error);

	/* read past the end of dst */
	ret = fu_memcpy_safe(dst, sizeof(dst), 0x0, src, sizeof(src), 0x0, 6, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_READ);
	g_assert_false(ret);
	g_clear_error(&error);

	/* read past the end of src with offset */
	ret = fu_memcpy_safe(dst, sizeof(dst), 0x0, src, sizeof(src), 0x4, 4, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_READ);
	g_assert_false(ret);
	g_clear_error(&error);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/mem/memmem", fu_mem_memmem_func);
	g_test_add_func("/fwupd/mem/endian", fu_mem_endian_func);
	g_test_add_func("/fwupd/mem/memcpy", fu_mem_memcpy_func);
	return g_test_run();
}
