/*
 * Copyright 2025 Harris Tai <harris_tai@pixart.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-pixart-tp-firmware.h"

static void
fu_pixart_tp_firmware_xml_func(void)
{
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autoptr(GError) error = NULL;

	filename = g_test_build_filename(G_TEST_DIST, "tests", "pixart-tp.builder.xml", NULL);
	ret = fu_firmware_roundtrip_from_filename(filename,
						  "a9ed17b970a867c190f62be59338dbad89d07553",
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
	g_type_ensure(FU_TYPE_PIXART_TP_FIRMWARE);
	g_test_add_func("/pixart-tp/firmware{xml}", fu_pixart_tp_firmware_xml_func);
	return g_test_run();
}
