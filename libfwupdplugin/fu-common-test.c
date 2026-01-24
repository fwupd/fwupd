/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-test.h"

static void
fu_common_align_up_func(void)
{
	g_assert_cmpint(fu_common_align_up(0, 0), ==, 0);
	g_assert_cmpint(fu_common_align_up(5, 0), ==, 5);
	g_assert_cmpint(fu_common_align_up(5, 3), ==, 8);
	g_assert_cmpint(fu_common_align_up(1023, 10), ==, 1024);
	g_assert_cmpint(fu_common_align_up(1024, 10), ==, 1024);
	g_assert_cmpint(fu_common_align_up(G_MAXSIZE - 1, 10), ==, G_MAXSIZE);
}

static void
fu_common_checked_add_func(void)
{
	g_assert_cmpint(fu_size_checked_add(0, 0), ==, 0);
	g_assert_cmpint(fu_size_checked_add(0, 42), ==, 42);
	g_assert_cmpint(fu_size_checked_add(42, 0), ==, 42);
	g_assert_cmpint(fu_size_checked_add(G_MAXSIZE / 2, G_MAXSIZE / 2), ==, G_MAXSIZE - 1);
	g_assert_cmpint(fu_size_checked_add(G_MAXSIZE, 1), ==, G_MAXSIZE);
	g_assert_cmpint(fu_size_checked_add(G_MAXSIZE, G_MAXSIZE), ==, G_MAXSIZE);
}

static void
fu_common_error_map_func(void)
{
	const FuErrorMapEntry entries[] = {
	    {0, FWUPD_ERROR_LAST, NULL},
	    {1, FWUPD_ERROR_NOT_SUPPORTED, "not supported"},
	};
	gboolean ret;
	g_autoptr(GError) error1 = NULL;
	g_autoptr(GError) error2 = NULL;
	g_autoptr(GError) error3 = NULL;

	ret = fu_error_map_entry_to_gerror(0, entries, G_N_ELEMENTS(entries), &error1);
	g_assert_no_error(error1);
	g_assert_true(ret);
	ret = fu_error_map_entry_to_gerror(1, entries, G_N_ELEMENTS(entries), &error2);
	g_assert_error(error2, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_false(ret);
	ret = fu_error_map_entry_to_gerror(255, entries, G_N_ELEMENTS(entries), &error3);
	g_assert_error(error3, FWUPD_ERROR, FWUPD_ERROR_INTERNAL);
	g_assert_false(ret);
}

static void
fu_common_bitwise_func(void)
{
	guint64 val = 0;

	g_assert_true(FU_BIT_IS_CLEAR(val, 1));
	g_assert_true(FU_BIT_IS_CLEAR(val, 63));
	g_assert_false(FU_BIT_IS_SET(val, 1));
	g_assert_false(FU_BIT_IS_SET(val, 63));

	FU_BIT_SET(val, 1);
	FU_BIT_SET(val, 63);
	g_assert_true(FU_BIT_IS_SET(val, 1));
	g_assert_true(FU_BIT_IS_SET(val, 63));
	g_assert_cmpint(val, ==, 0x8000000000000002ull);

	FU_BIT_CLEAR(val, 1);
	g_assert_cmpint(val, ==, 0x8000000000000000ull);
	FU_BIT_CLEAR(val, 63);
	g_assert_cmpint(val, ==, 0);
}

static void
fu_common_crc_func(void)
{
	guint8 buf[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09};

	g_assert_cmpint(fu_crc8(FU_CRC_KIND_B8_STANDARD, buf, sizeof(buf)), ==, (guint8)~0x7A);
	g_assert_cmpint(fu_crc16(FU_CRC_KIND_B16_USB, buf, sizeof(buf)), ==, 0x4DF1);
	g_assert_cmpint(fu_crc_misr16(0, buf, (sizeof(buf) / 2) * 2), ==, 0x40D);
	g_assert_cmpint(fu_crc_misr16(0xFFFF, buf, (sizeof(buf) / 2) * 2), ==, 0xFBFA);

	/* all the CRC32 variants, verified using https://crccalc.com/?method=CRC-32 */
	g_assert_cmpint(fu_crc32(FU_CRC_KIND_B32_STANDARD, buf, sizeof(buf)), ==, 0x40EFAB9E);
	g_assert_cmpint(fu_crc32(FU_CRC_KIND_B32_BZIP2, buf, sizeof(buf)), ==, 0x89AE7A5C);
	g_assert_cmpint(fu_crc32(FU_CRC_KIND_B32_JAMCRC, buf, sizeof(buf)), ==, 0xBF105461);
	g_assert_cmpint(fu_crc32(FU_CRC_KIND_B32_MPEG2, buf, sizeof(buf)), ==, 0x765185A3);
	g_assert_cmpint(fu_crc32(FU_CRC_KIND_B32_POSIX, buf, sizeof(buf)), ==, 0x037915C4);
	g_assert_cmpint(fu_crc32(FU_CRC_KIND_B32_SATA, buf, sizeof(buf)), ==, 0xBA55CCAC);
	g_assert_cmpint(fu_crc32(FU_CRC_KIND_B32_XFER, buf, sizeof(buf)), ==, 0x868E70FC);
	g_assert_cmpint(fu_crc32(FU_CRC_KIND_B32C, buf, sizeof(buf)), ==, 0x5A14B9F9);
	g_assert_cmpint(fu_crc32(FU_CRC_KIND_B32D, buf, sizeof(buf)), ==, 0x68AD8D3C);
	g_assert_cmpint(fu_crc32(FU_CRC_KIND_B32Q, buf, sizeof(buf)), ==, 0xE955C875);
}

static void
fu_common_guid_func(void)
{
	gboolean ret;
	guint8 buf[16] = {0};

	ret = fu_common_guid_is_plausible(buf);
	g_assert_false(ret);

	buf[0] = 0x5;
	ret = fu_common_guid_is_plausible(buf);
	g_assert_false(ret);

	for (guint i = 0; i < sizeof(buf); i++)
		buf[i] = 0xFF;
	ret = fu_common_guid_is_plausible(buf);
	g_assert_true(ret);
}

static void
fu_common_olson_timezone_id_func(void)
{
	g_autofree gchar *timezone_id = NULL;
	g_autoptr(GError) error = NULL;

#ifdef HOST_MACHINE_SYSTEM_DARWIN
	g_test_skip("not supported on Darwin");
	return;
#endif

	timezone_id = fu_common_get_olson_timezone_id(&error);
	g_assert_no_error(error);
#ifdef _WIN32
	/* we do not emulate this on Windows, so just check for anything */
	g_assert_nonnull(timezone_id);
#else
	g_assert_cmpstr(timezone_id, ==, "America/New_York");
#endif
}

static void
fu_common_cpuid_func(void)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GHashTable) cpu_attrs = NULL;

	cpu_attrs = fu_cpu_get_attrs(&error);
	g_assert_no_error(error);
	g_assert_nonnull(cpu_attrs);
	g_assert_cmpstr(g_hash_table_lookup(cpu_attrs, "vendor_id"), ==, "AuthenticAMD");
	g_assert_cmpstr(g_hash_table_lookup(cpu_attrs, "fpu_exception"), ==, "yes");
}

int
main(int argc, char **argv)
{
	g_autofree gchar *testdatadir = NULL;
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	testdatadir = g_test_build_filename(G_TEST_DIST, "tests", NULL);
	(void)g_setenv("FWUPD_SYSCONFDIR", testdatadir, TRUE);
	(void)g_setenv("FWUPD_PROCFS", testdatadir, TRUE);
	g_test_add_func("/fwupd/common/olson-timezone-id", fu_common_olson_timezone_id_func);
	g_test_add_func("/fwupd/common/checked-add", fu_common_checked_add_func);
	g_test_add_func("/fwupd/common/error-map", fu_common_error_map_func);
	g_test_add_func("/fwupd/common/align-up", fu_common_align_up_func);
	g_test_add_func("/fwupd/common/bitwise", fu_common_bitwise_func);
	g_test_add_func("/fwupd/common/crc", fu_common_crc_func);
	g_test_add_func("/fwupd/common/guid", fu_common_guid_func);
	g_test_add_func("/fwupd/common/cpuid", fu_common_cpuid_func);
	return g_test_run();
}
