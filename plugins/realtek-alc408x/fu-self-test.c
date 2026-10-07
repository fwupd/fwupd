/*
 * Copyright 2026 NVIDIA Corporation
 * Author: Vishnu Raghav <vraghav@nvidia.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-realtek-alc408x-common.h"
#include "fu-realtek-alc408x-firmware.h"

static void
fu_realtek_alc408x_version_func(void)
{
	g_autofree gchar *version1 = fu_realtek_alc408x_version_to_string(0x100200030004000Aull);
	g_autofree gchar *version2 = fu_realtek_alc408x_version_to_string(0x1002000300040005ull);
	g_assert_cmpstr(version1, ==, "1.002.0003-0004.000A");
	g_assert_cmpstr(version2, ==, "1.002.0003-0004.0005");
	g_assert_cmpint(g_strcmp0(version1, version2), >, 0);
}

static void
fu_realtek_alc408x_firmware_xml_func(void)
{
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autoptr(GError) error = NULL;

	filename = g_test_build_filename(G_TEST_DIST, "tests", "realtek-alc408x.builder.xml", NULL);
	ret = fu_firmware_roundtrip_from_filename(filename,
						  "3d7a78c3207609824759e151cad1cb38d48b932f",
						  FU_FIRMWARE_BUILDER_FLAG_NONE,
						  &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	g_type_ensure(FU_TYPE_REALTEK_ALC408X_FIRMWARE);
	g_test_add_func("/fwupd/realtek-alc408x/version", fu_realtek_alc408x_version_func);
	g_test_add_func("/fwupd/realtek-alc408x/firmware/xml",
			fu_realtek_alc408x_firmware_xml_func);
	return g_test_run();
}
