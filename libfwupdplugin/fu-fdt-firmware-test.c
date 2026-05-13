/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-context-private.h"

static void
fu_fdt_firmware_func(void)
{
	gboolean ret;
	g_autofree gchar *fn = NULL;
	g_autofree gchar *compatible = NULL;
	g_autofree gchar *testdatadir = NULL;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuFirmware) fdt = NULL;
	g_autoptr(FuFirmware) fdt_root = NULL;
	g_autoptr(FuFirmware) fdt_tmp = NULL;
	g_autoptr(FuFirmware) img2 = NULL;
	g_autoptr(FuFirmware) img3 = NULL;
	g_autoptr(FuFirmware) img4 = NULL;
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GFile) file = NULL;

	fu_context_add_firmware_gtypes(ctx);

	tmpdir = fu_temporary_directory_new("fdt", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);

	/* set up test harness */
	testdatadir = g_test_build_filename(G_TEST_DIST, "tests", NULL);
	fu_context_set_tmpdir(ctx, FU_PATH_KIND_LOCALSTATEDIR_PKG, tmpdir);

	/* write file */
	fdt_tmp = fu_firmware_new_from_xml(
	    "<firmware gtype=\"FuFdtFirmware\">\n"
	    "  <firmware gtype=\"FuFdtImage\">\n"
	    "    <metadata key=\"compatible\" format=\"str\">pine64,rockpro64-v2.1</metadata>\n"
	    "  </firmware>\n"
	    "</firmware>\n",
	    &error);
	g_assert_no_error(error);
	g_assert_nonnull(fdt_tmp);
	fn = fu_temporary_directory_build(tmpdir, "system.dtb", NULL);
	file = g_file_new_for_path(fn);
	ret = fu_firmware_write_file(FU_FIRMWARE(fdt_tmp), file, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* get compatible from the context */
	fdt = fu_context_get_fdt(ctx, &error);
	g_assert_no_error(error);
	g_assert_nonnull(fdt);
	fdt_root = fu_firmware_get_image_by_id(fdt, NULL, &error);
	g_assert_no_error(error);
	g_assert_nonnull(fdt_root);
	ret = fu_fdt_image_get_attr_str(FU_FDT_IMAGE(fdt_root), "compatible", &compatible, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpstr(compatible, ==, "pine64,rockpro64-v2.1");

	/* get by GType */
	img2 = fu_firmware_get_image_by_gtype(fdt, FU_TYPE_FIRMWARE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(img2);
	img3 = fu_firmware_get_image_by_gtype(fdt, FU_TYPE_FDT_IMAGE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(img3);
	img4 = fu_firmware_get_image_by_gtype(fdt, G_TYPE_STRING, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_null(img4);
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/fdt-firmware", fu_fdt_firmware_func);
	return g_test_run();
}
