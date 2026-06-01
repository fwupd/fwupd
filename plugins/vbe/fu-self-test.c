/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-context-private.h"
#include "fu-plugin-private.h"
#include "fu-vbe-plugin.h"
#include "fu-vbe-simple-device.h"

static FuTemporaryDirectory *
fu_vbe_test_write_fdt(FuContext *ctx, const gchar *xml, GError **error)
{
	g_autofree gchar *fn = NULL;
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(FuFirmware) fdt_tmp = NULL;
	g_autoptr(GFile) file = NULL;

	fdt_tmp = fu_firmware_new_from_xml(xml, error);
	if (fdt_tmp == NULL)
		return NULL;
	tmpdir = fu_temporary_directory_new("vbe", error);
	if (tmpdir == NULL)
		return NULL;
	fu_context_set_tmpdir(ctx, FU_PATH_KIND_LOCALSTATEDIR_PKG, tmpdir);
	fn = fu_temporary_directory_build(tmpdir, "system.dtb", NULL);
	file = g_file_new_for_path(fn);
	if (!fu_firmware_write_file(FU_FIRMWARE(fdt_tmp), file, error))
		return NULL;
	return g_steal_pointer(&tmpdir);
}

static void
fu_vbe_plugin_coldplug_func(void)
{
	gboolean ret;
	GPtrArray *devices;
	FuDevice *dev;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuPlugin) plugin = fu_plugin_new_from_gtype(fu_vbe_plugin_get_type(), ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(GError) error = NULL;

	fu_context_add_firmware_gtypes(ctx);

	tmpdir = fu_vbe_test_write_fdt(
	    ctx,
	    "<firmware gtype=\"FuFdtFirmware\">\n"
	    "  <firmware gtype=\"FuFdtImage\">\n"
	    "    <metadata key=\"compatible\" format=\"str\">pine64,rockpro64-v2.1</metadata>\n"
	    "    <firmware gtype=\"FuFdtImage\">\n"
	    "      <id>chosen</id>\n"
	    "      <firmware gtype=\"FuFdtImage\">\n"
	    "        <id>fwupd</id>\n"
	    "        <firmware gtype=\"FuFdtImage\">\n"
	    "          <id>firmware</id>\n"
	    "          <metadata key=\"compatible\" format=\"str\">fwupd,vbe-simple</metadata>\n"
	    "          <metadata key=\"cur-version\" format=\"str\">1.2.3</metadata>\n"
	    "          <metadata key=\"bootloader-version\" format=\"str\">2022.01</metadata>\n"
	    "          <metadata key=\"storage\" format=\"str\">/tmp/testfw</metadata>\n"
	    "          <metadata key=\"area-start\" format=\"uint32\">0x100000</metadata>\n"
	    "          <metadata key=\"area-size\" format=\"uint32\">0x200000</metadata>\n"
	    "        </firmware>\n"
	    "      </firmware>\n"
	    "    </firmware>\n"
	    "  </firmware>\n"
	    "</firmware>\n",
	    &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);

	ret = fu_plugin_runner_coldplug(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	devices = fu_plugin_get_devices(plugin);
	g_assert_cmpint(devices->len, ==, 1);

	dev = g_ptr_array_index(devices, 0);
	g_assert_true(FU_IS_VBE_SIMPLE_DEVICE(dev));
}

static void
fu_vbe_plugin_coldplug_no_fdt_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuPlugin) plugin = fu_plugin_new_from_gtype(fu_vbe_plugin_get_type(), ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(GError) error = NULL;

	/* redirect so we never read the host FDT */
	tmpdir = fu_temporary_directory_new("vbe", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);
	fu_context_set_tmpdir(ctx, FU_PATH_KIND_LOCALSTATEDIR_PKG, tmpdir);
	fu_context_set_tmpdir(ctx, FU_PATH_KIND_SYSFSDIR_FW, tmpdir);

	ret = fu_plugin_runner_coldplug(plugin, progress, &error);
	g_assert_false(ret);
	g_assert_nonnull(error);
}

static void
fu_vbe_plugin_coldplug_invalid_compatible_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuPlugin) plugin = fu_plugin_new_from_gtype(fu_vbe_plugin_get_type(), ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(GError) error = NULL;

	fu_context_add_firmware_gtypes(ctx);

	tmpdir = fu_vbe_test_write_fdt(
	    ctx,
	    "<firmware gtype=\"FuFdtFirmware\">\n"
	    "  <firmware gtype=\"FuFdtImage\">\n"
	    "    <metadata key=\"compatible\" format=\"str\">pine64,rockpro64-v2.1</metadata>\n"
	    "    <firmware gtype=\"FuFdtImage\">\n"
	    "      <id>chosen</id>\n"
	    "      <firmware gtype=\"FuFdtImage\">\n"
	    "        <id>fwupd</id>\n"
	    "        <firmware gtype=\"FuFdtImage\">\n"
	    "          <id>firmware</id>\n"
	    "          <metadata key=\"compatible\" format=\"str\">fwupd,vbe-unknown</metadata>\n"
	    "        </firmware>\n"
	    "      </firmware>\n"
	    "    </firmware>\n"
	    "  </firmware>\n"
	    "</firmware>\n",
	    &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);

	g_test_expect_message("FuPluginVbe", G_LOG_LEVEL_WARNING, "*no driver for VBE method*");
	ret = fu_plugin_runner_coldplug(plugin, progress, &error);
	g_assert_false(ret);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/vbe/coldplug", fu_vbe_plugin_coldplug_func);
	g_test_add_func("/vbe/coldplug/no-fdt", fu_vbe_plugin_coldplug_no_fdt_func);
	g_test_add_func("/vbe/coldplug/invalid-compatible",
			fu_vbe_plugin_coldplug_invalid_compatible_func);
	return g_test_run();
}
