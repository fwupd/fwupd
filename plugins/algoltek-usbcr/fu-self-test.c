/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-algoltek-usbcr-firmware.h"

static void
fu_algoltek_usbcr_firmware_xml_func(void)
{
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autoptr(GError) error = NULL;

	filename = g_test_build_filename(G_TEST_DIST, "tests", "algoltek-usbcr.builder.xml", NULL);
	ret = fu_firmware_roundtrip_from_filename(filename,
						  "83bdad05844e42605637a5f5cdbfacb8afa65339",
						  FU_FIRMWARE_BUILDER_FLAG_NONE,
						  &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	(void)g_setenv("G_MESSAGES_DEBUG", "all", TRUE);

	g_test_init(&argc, &argv, NULL);

	/* only critical and error are fatal */
	g_log_set_fatal_mask(NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);

	/* tests go here */
	g_type_ensure(FU_TYPE_ALGOLTEK_USBCR_FIRMWARE);
	g_test_add_func("/fwupd/algoltek-usbcr/firmware{xml}", fu_algoltek_usbcr_firmware_xml_func);
	return g_test_run();
}
