/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-test.h"

static void
fu_version_guess_format_func(void)
{
	g_assert_cmpint(fu_version_guess_format(NULL), ==, FWUPD_VERSION_FORMAT_UNKNOWN);
	g_assert_cmpint(fu_version_guess_format(""), ==, FWUPD_VERSION_FORMAT_UNKNOWN);
	g_assert_cmpint(fu_version_guess_format("1234ac"), ==, FWUPD_VERSION_FORMAT_HEX);
	g_assert_cmpint(fu_version_guess_format("1.2"), ==, FWUPD_VERSION_FORMAT_PAIR);
	g_assert_cmpint(fu_version_guess_format("1.2.3"), ==, FWUPD_VERSION_FORMAT_TRIPLET);
	g_assert_cmpint(fu_version_guess_format("1.2.3.4"), ==, FWUPD_VERSION_FORMAT_QUAD);
	g_assert_cmpint(fu_version_guess_format("1.2.3.4.5"), ==, FWUPD_VERSION_FORMAT_UNKNOWN);
	g_assert_cmpint(fu_version_guess_format("1a.2b.3"), ==, FWUPD_VERSION_FORMAT_PLAIN);
	g_assert_cmpint(fu_version_guess_format("1"), ==, FWUPD_VERSION_FORMAT_NUMBER);
	g_assert_cmpint(fu_version_guess_format("1A"), ==, FWUPD_VERSION_FORMAT_HEX);
	g_assert_cmpint(fu_version_guess_format("0x10201"), ==, FWUPD_VERSION_FORMAT_NUMBER);
}

static void
fu_version_verify_format_func(void)
{
	gboolean ret;
	g_autoptr(GError) error = NULL;

	ret = fu_version_verify_format("1A", FWUPD_VERSION_FORMAT_HEX, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_version_verify_format("1A", FWUPD_VERSION_FORMAT_NUMBER, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_false(ret);
}

static void
fu_version_semver_func(void)
{
	struct {
		const gchar *old;
		const gchar *new;
		FwupdVersionFormat fmt;
	} map[] = {{"1.2.3", "1.2.3", FWUPD_VERSION_FORMAT_TRIPLET},
		   {"1.2.3.4", "1.2.3", FWUPD_VERSION_FORMAT_TRIPLET},
		   {"1.2", "0.1.2", FWUPD_VERSION_FORMAT_TRIPLET},
		   {"1", "0.0.1", FWUPD_VERSION_FORMAT_TRIPLET},
		   {"CBET1.2.3", "1.2.3", FWUPD_VERSION_FORMAT_TRIPLET},
		   {"4.11-1190-g12d8072e6b-dirty", "4.11.1190", FWUPD_VERSION_FORMAT_TRIPLET},
		   {"4.11-1190-g12d8072e6b-dirty", "4.11", FWUPD_VERSION_FORMAT_PAIR},
		   {NULL, NULL}};
	for (guint i = 0; map[i].old != NULL; i++) {
		g_autofree gchar *tmp = fu_version_ensure_semver(map[i].old, map[i].fmt);
		g_assert_cmpstr(tmp, ==, map[i].new);
	}
}

static void
fu_version_func(void)
{
	guint i;
	struct {
		guint32 val;
		const gchar *ver;
		FwupdVersionFormat flags;
	} version_from_uint32[] = {
	    {0x0, "0.0.0.0", FWUPD_VERSION_FORMAT_QUAD},
	    {0xff, "0.0.0.255", FWUPD_VERSION_FORMAT_QUAD},
	    {0xff01, "0.0.255.1", FWUPD_VERSION_FORMAT_QUAD},
	    {0xff0001, "0.255.0.1", FWUPD_VERSION_FORMAT_QUAD},
	    {0xff000100, "255.0.1.0", FWUPD_VERSION_FORMAT_QUAD},
	    {0x0, "0.0.0", FWUPD_VERSION_FORMAT_TRIPLET},
	    {0xff, "0.0.255", FWUPD_VERSION_FORMAT_TRIPLET},
	    {0xff01, "0.0.65281", FWUPD_VERSION_FORMAT_TRIPLET},
	    {0xff0001, "0.255.1", FWUPD_VERSION_FORMAT_TRIPLET},
	    {0xff000100, "255.0.256", FWUPD_VERSION_FORMAT_TRIPLET},
	    {0x0, "0", FWUPD_VERSION_FORMAT_NUMBER},
	    {0xff000100, "4278190336", FWUPD_VERSION_FORMAT_NUMBER},
	    {0x0, "11.0.0.0", FWUPD_VERSION_FORMAT_INTEL_ME},
	    {0xffffffff, "18.31.255.65535", FWUPD_VERSION_FORMAT_INTEL_ME},
	    {0x0b32057a, "11.11.50.1402", FWUPD_VERSION_FORMAT_INTEL_ME},
	    {0xb8320d84, "11.8.50.3460", FWUPD_VERSION_FORMAT_INTEL_ME2},
	    {0x00000741, "19.0.0.1857", FWUPD_VERSION_FORMAT_INTEL_CSME19},
	    {0x226a4b00, "137.2706.768", FWUPD_VERSION_FORMAT_SURFACE_LEGACY},
	    {0x6001988, "6.25.136", FWUPD_VERSION_FORMAT_SURFACE},
	    {0x00ff0001, "255.0.1", FWUPD_VERSION_FORMAT_DELL_BIOS},
	    {0x010f0201, "1.15.2", FWUPD_VERSION_FORMAT_DELL_BIOS_MSB},
	    {0xc8, "0x000000c8", FWUPD_VERSION_FORMAT_HEX},
	};
	struct {
		guint32 val;
		const gchar *ver;
		FwupdVersionFormat flags;
	} version_from_uint24[] = {
	    {0x0, "0.0.0", FWUPD_VERSION_FORMAT_TRIPLET},
	    {0xff, "0.0.255", FWUPD_VERSION_FORMAT_TRIPLET},
	    {0x0, "0", FWUPD_VERSION_FORMAT_NUMBER},
	    {0xc8, "0x0000c8", FWUPD_VERSION_FORMAT_HEX},
	};
	struct {
		guint64 val;
		const gchar *ver;
		FwupdVersionFormat flags;
	} version_from_uint64[] = {
	    {0x0, "0.0.0.0", FWUPD_VERSION_FORMAT_QUAD},
	    {0xff, "0.0.0.255", FWUPD_VERSION_FORMAT_QUAD},
	    {0xffffffffffffffff, "65535.65535.65535.65535", FWUPD_VERSION_FORMAT_QUAD},
	    {0xff, "0.255", FWUPD_VERSION_FORMAT_PAIR},
	    {0xffffffffffffffff, "4294967295.4294967295", FWUPD_VERSION_FORMAT_PAIR},
	    {0x0, "0", FWUPD_VERSION_FORMAT_NUMBER},
	    {0x11000000c8, "0x00000011000000c8", FWUPD_VERSION_FORMAT_HEX},
	};
	struct {
		guint16 val;
		const gchar *ver;
		FwupdVersionFormat flags;
	} version_from_uint16[] = {
	    {0x0, "0.0", FWUPD_VERSION_FORMAT_PAIR},
	    {0xff, "0.255", FWUPD_VERSION_FORMAT_PAIR},
	    {0xff01, "255.1", FWUPD_VERSION_FORMAT_PAIR},
	    {0x0, "0.0", FWUPD_VERSION_FORMAT_BCD},
	    {0x0110, "1.10", FWUPD_VERSION_FORMAT_BCD},
	    {0x9999, "99.99", FWUPD_VERSION_FORMAT_BCD},
	    {0x0, "0", FWUPD_VERSION_FORMAT_NUMBER},
	    {0x1234, "4660", FWUPD_VERSION_FORMAT_NUMBER},
	    {0x1234, "1.2.52", FWUPD_VERSION_FORMAT_TRIPLET},
	};
	struct {
		const gchar *old;
		const gchar *new;
	} version_parse[] = {
	    {"0", "0"},
	    {"0x1a", "0.0.26"},
	    {"257", "0.0.257"},
	    {"1.2.3", "1.2.3"},
	    {"0xff0001", "0.255.1"},
	    {"16711681", "0.255.1"},
	    {"20150915", "20150915"},
	    {"dave", "dave"},
	    {"0x1x", "0x1x"},
	};

	/* check version conversion */
	for (i = 0; i < G_N_ELEMENTS(version_from_uint64); i++) {
		g_autofree gchar *ver = NULL;
		ver = fu_version_from_uint64(version_from_uint64[i].val,
					     version_from_uint64[i].flags);
		g_assert_cmpstr(ver, ==, version_from_uint64[i].ver);
	}
	for (i = 0; i < G_N_ELEMENTS(version_from_uint32); i++) {
		g_autofree gchar *ver = NULL;
		ver = fu_version_from_uint32(version_from_uint32[i].val,
					     version_from_uint32[i].flags);
		g_assert_cmpstr(ver, ==, version_from_uint32[i].ver);
	}
	for (i = 0; i < G_N_ELEMENTS(version_from_uint24); i++) {
		g_autofree gchar *ver = NULL;
		ver = fu_version_from_uint24(version_from_uint24[i].val,
					     version_from_uint24[i].flags);
		g_assert_cmpstr(ver, ==, version_from_uint24[i].ver);
	}
	for (i = 0; i < G_N_ELEMENTS(version_from_uint16); i++) {
		g_autofree gchar *ver = NULL;
		ver = fu_version_from_uint16(version_from_uint16[i].val,
					     version_from_uint16[i].flags);
		g_assert_cmpstr(ver, ==, version_from_uint16[i].ver);
	}

	/* check version parsing */
	for (i = 0; i < G_N_ELEMENTS(version_parse); i++) {
		g_autofree gchar *ver = NULL;
		ver = fu_version_parse_from_format(version_parse[i].old,
						   FWUPD_VERSION_FORMAT_TRIPLET);
		g_assert_cmpstr(ver, ==, version_parse[i].new);
	}
}

static void
fu_version_vercmp_func(void)
{
	/* same */
	g_assert_cmpint(fu_version_compare("1.2.3", "1.2.3", FWUPD_VERSION_FORMAT_UNKNOWN), ==, 0);
	g_assert_cmpint(
	    fu_version_compare("001.002.003", "001.002.003", FWUPD_VERSION_FORMAT_UNKNOWN),
	    ==,
	    0);
	g_assert_cmpint(fu_version_compare("0x00000002", "0x2", FWUPD_VERSION_FORMAT_HEX), ==, 0);

	/* upgrade and downgrade */
	g_assert_cmpint(fu_version_compare("1.2.3", "1.2.4", FWUPD_VERSION_FORMAT_UNKNOWN), <, 0);
	g_assert_cmpint(
	    fu_version_compare("001.002.000", "001.002.009", FWUPD_VERSION_FORMAT_UNKNOWN),
	    <,
	    0);
	g_assert_cmpint(fu_version_compare("1.2.3", "1.2.2", FWUPD_VERSION_FORMAT_UNKNOWN), >, 0);
	g_assert_cmpint(
	    fu_version_compare("001.002.009", "001.002.000", FWUPD_VERSION_FORMAT_UNKNOWN),
	    >,
	    0);

	/* unequal depth */
	g_assert_cmpint(fu_version_compare("1.2.3", "1.2.3.1", FWUPD_VERSION_FORMAT_UNKNOWN), <, 0);
	g_assert_cmpint(fu_version_compare("1.2.3.1", "1.2.4", FWUPD_VERSION_FORMAT_UNKNOWN), <, 0);

	/* mixed-alpha-numeric */
	g_assert_cmpint(fu_version_compare("1.2.3a", "1.2.3a", FWUPD_VERSION_FORMAT_UNKNOWN),
			==,
			0);
	g_assert_cmpint(fu_version_compare("1.2.3a", "1.2.3b", FWUPD_VERSION_FORMAT_UNKNOWN), <, 0);
	g_assert_cmpint(fu_version_compare("1.2.3b", "1.2.3a", FWUPD_VERSION_FORMAT_UNKNOWN), >, 0);

	/* alpha version append */
	g_assert_cmpint(fu_version_compare("1.2.3", "1.2.3a", FWUPD_VERSION_FORMAT_UNKNOWN), <, 0);
	g_assert_cmpint(fu_version_compare("1.2.3a", "1.2.3", FWUPD_VERSION_FORMAT_UNKNOWN), >, 0);

	/* alpha only */
	g_assert_cmpint(fu_version_compare("alpha", "alpha", FWUPD_VERSION_FORMAT_UNKNOWN), ==, 0);
	g_assert_cmpint(fu_version_compare("alpha", "beta", FWUPD_VERSION_FORMAT_UNKNOWN), <, 0);
	g_assert_cmpint(fu_version_compare("beta", "alpha", FWUPD_VERSION_FORMAT_UNKNOWN), >, 0);

	/* alpha-compare */
	g_assert_cmpint(fu_version_compare("1.2a.3", "1.2a.3", FWUPD_VERSION_FORMAT_UNKNOWN),
			==,
			0);
	g_assert_cmpint(fu_version_compare("1.2a.3", "1.2b.3", FWUPD_VERSION_FORMAT_UNKNOWN), <, 0);
	g_assert_cmpint(fu_version_compare("1.2b.3", "1.2a.3", FWUPD_VERSION_FORMAT_UNKNOWN), >, 0);

	/* tilde is all-powerful */
	g_assert_cmpint(fu_version_compare("1.2.3~rc1", "1.2.3~rc1", FWUPD_VERSION_FORMAT_UNKNOWN),
			==,
			0);
	g_assert_cmpint(fu_version_compare("1.2.3~rc1", "1.2.3", FWUPD_VERSION_FORMAT_UNKNOWN),
			<,
			0);
	g_assert_cmpint(fu_version_compare("1.2.3", "1.2.3~rc1", FWUPD_VERSION_FORMAT_UNKNOWN),
			>,
			0);
	g_assert_cmpint(fu_version_compare("1.2.3~rc2", "1.2.3~rc1", FWUPD_VERSION_FORMAT_UNKNOWN),
			>,
			0);

	/* invalid */
	g_assert_cmpint(fu_version_compare("1", NULL, FWUPD_VERSION_FORMAT_UNKNOWN), ==, G_MAXINT);
	g_assert_cmpint(fu_version_compare(NULL, "1", FWUPD_VERSION_FORMAT_UNKNOWN), ==, G_MAXINT);
	g_assert_cmpint(fu_version_compare(NULL, NULL, FWUPD_VERSION_FORMAT_UNKNOWN), ==, G_MAXINT);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/version", fu_version_func);
	g_test_add_func("/fwupd/version/guess-format", fu_version_guess_format_func);
	g_test_add_func("/fwupd/version/verify-format", fu_version_verify_format_func);
	g_test_add_func("/fwupd/version/semver", fu_version_semver_func);
	g_test_add_func("/fwupd/version/vercmp", fu_version_vercmp_func);
	return g_test_run();
}
