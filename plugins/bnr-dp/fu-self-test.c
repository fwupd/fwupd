/*
 * Copyright 2025 B&R Industrial Automation GmbH
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-bnr-dp-firmware.h"

static void
fu_bnr_dp_firmware_xml_func(void)
{
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autoptr(GError) error = NULL;

	filename = g_test_build_filename(G_TEST_DIST, "tests", "bnr-dp.builder.xml", NULL);
	ret = fu_firmware_roundtrip_from_filename(filename,
						  "b83504af44c2a53561f9e5f25fb133903e1c19fc",
						  FU_FIRMWARE_BUILDER_FLAG_NO_WRITE,
						  &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);

	/* tests go here */
	g_type_ensure(FU_TYPE_BNR_DP_FIRMWARE);
	g_test_add_func("/bnr-dp/firmware{xml}", fu_bnr_dp_firmware_xml_func);
	return g_test_run();
}
