/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-synaptics-rmi-firmware.h"

static void
fu_synaptics_rmi_firmware_0x_func(void)
{
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autoptr(GError) error = NULL;

	filename =
	    g_test_build_filename(G_TEST_DIST, "tests", "synaptics-rmi-0x.builder.xml", NULL);
	ret = fu_firmware_roundtrip_from_filename(filename,
						  "8b097c034028a69e6416bcc39f312e2fa9247381",
						  FU_FIRMWARE_BUILDER_FLAG_NO_BINARY_COMPARE,
						  &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_synaptics_rmi_firmware_10_func(void)
{
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autoptr(GError) error = NULL;

	filename =
	    g_test_build_filename(G_TEST_DIST, "tests", "synaptics-rmi-10.builder.xml", NULL);
	ret = fu_firmware_roundtrip_from_filename(filename,
						  "bd85539bb100e5bd6debb00b06b5a7e7fa9bd030",
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
	g_type_ensure(FU_TYPE_SYNAPTICS_RMI_FIRMWARE);
	g_test_add_func("/synaptics-rmi/firmware{0x}", fu_synaptics_rmi_firmware_0x_func);
	g_test_add_func("/synaptics-rmi/firmware{10}", fu_synaptics_rmi_firmware_10_func);
	return g_test_run();
}
