/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-acpi-sbom-plugin.h"
#include "fu-context-private.h"
#include "fu-plugin-private.h"
#include "fu-smbios-private.h"

static void
fu_acpi_sbom_plugin_func(void)
{
	gboolean ret;
	g_autofree gchar *filename_bin = NULL;
	g_autofree gchar *filename_xml = NULL;
	g_autofree gchar *filename_cdx = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(FuFirmware) firmware2 = NULL;
	g_autoptr(FuFirmware) img = NULL;
	g_autoptr(GBytes) blob_img = NULL;
	g_autoptr(GPtrArray) imgs = NULL;
	g_autoptr(FuPlugin) plugin = g_object_new(FU_TYPE_ACPI_SBOM_PLUGIN, "context", ctx, NULL);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;

	/* set up test harness */
	tmpdir = fu_temporary_directory_new("acpi-sbom", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);
	fu_context_set_tmpdir(ctx, FU_PATH_KIND_ACPI_TABLES, tmpdir);
	fu_context_set_tmpdir(ctx, FU_PATH_KIND_LOCALSTATEDIR_PKG, tmpdir);

	/* register plugin GTypes */
	fu_plugin_runner_init(plugin);

	/* build a SBOM blob */
	fu_context_add_firmware_gtypes(ctx);
	filename_xml = g_test_build_filename(G_TEST_DIST, "tests", "acpi-sbom.builder.xml", NULL);
	g_assert_nonnull(filename_xml);
	firmware = fu_firmware_new_from_filename(filename_xml, &error);
	g_assert_no_error(error);
	g_assert_nonnull(firmware);
	blob = fu_firmware_write(firmware, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob);
	filename_bin = fu_temporary_directory_build(tmpdir, "SBOM", NULL);
	ret = fu_bytes_set_contents(filename_bin, blob, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* parse it back, the vendor-compressed entry should be skipped */
	firmware2 = g_object_new(g_type_from_name("FuAcpiSbomTable"), NULL);
	ret =
	    fu_firmware_parse_bytes(firmware2, blob, 0x0, FU_FIRMWARE_PARSE_FLAG_NO_SEARCH, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	imgs = fu_firmware_get_images(firmware2);
	g_assert_cmpint(imgs->len, ==, 1);
	img = g_object_ref(g_ptr_array_index(imgs, 0));
	blob_img = fu_firmware_get_bytes(img, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob_img);
	g_assert_cmpmem(g_bytes_get_data(blob_img, NULL),
			g_bytes_get_size(blob_img),
			"hello world",
			11);

	/* load it using the plugin */
	ret = fu_plugin_runner_coldplug(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/acpi-sbom/plugin", fu_acpi_sbom_plugin_func);
	return g_test_run();
}
