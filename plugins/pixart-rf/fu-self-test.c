/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-pixart-rf-firmware.h"

static void
fu_pixart_rf_firmware_xml_func(void)
{
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autoptr(GError) error = NULL;

	filename = g_test_build_filename(G_TEST_DIST, "tests", "pixart-rf.builder.xml", NULL);
	ret = fu_firmware_roundtrip_from_filename(filename,
						  "61efee24b3fad7358ce64144300e9e004b825759",
						  FU_FIRMWARE_BUILDER_FLAG_NO_BINARY_COMPARE,
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
	g_type_ensure(FU_TYPE_PIXART_RF_FIRMWARE);
	g_test_add_func("/pixart-rf/firmware{xml}", fu_pixart_rf_firmware_xml_func);
	return g_test_run();
}
