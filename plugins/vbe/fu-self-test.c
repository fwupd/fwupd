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

/* vbe-simple treats area-size as the end of its area, not its size */
#define FU_VBE_SIMPLE_TEST_AREA_START 0x1000
#define FU_VBE_SIMPLE_TEST_AREA_SIZE  0x3000

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

/* create a vbe-simple device for a board, as the plugin does */
static FuDevice *
fu_vbe_simple_test_device_new(FuContext *ctx, const gchar *storage)
{
	g_autofree gchar *xml = NULL;
	g_autoptr(FuFirmware) fdt = NULL;
	g_autoptr(FuFdtImage) fdt_root = NULL;
	g_autoptr(FuFdtImage) fdt_node = NULL;
	g_autoptr(GError) error = NULL;

	xml = g_strdup_printf(
	    "<firmware gtype=\"FuFdtFirmware\">\n"
	    "  <firmware gtype=\"FuFdtImage\">\n"
	    "    <firmware gtype=\"FuFdtImage\">\n"
	    "      <id>chosen</id>\n"
	    "      <firmware gtype=\"FuFdtImage\">\n"
	    "        <id>fwupd</id>\n"
	    "        <metadata key=\"compatible\" format=\"str\">pine64,rockpro64-v2.1</metadata>\n"
	    "        <firmware gtype=\"FuFdtImage\">\n"
	    "          <id>firmware</id>\n"
	    "          <metadata key=\"compatible\" format=\"str\">fwupd,vbe-simple</metadata>\n"
	    "          <metadata key=\"storage\" format=\"str\">%s</metadata>\n"
	    "          <metadata key=\"area-start\" format=\"uint32\">0x%x</metadata>\n"
	    "          <metadata key=\"area-size\" format=\"uint32\">0x%x</metadata>\n"
	    "        </firmware>\n"
	    "      </firmware>\n"
	    "    </firmware>\n"
	    "  </firmware>\n"
	    "</firmware>\n",
	    storage,
	    (guint)FU_VBE_SIMPLE_TEST_AREA_START,
	    (guint)FU_VBE_SIMPLE_TEST_AREA_SIZE);
	fu_context_add_firmware_gtypes(ctx);
	fdt = fu_firmware_new_from_xml(xml, &error);
	g_assert_no_error(error);
	g_assert_nonnull(fdt);
	fdt_root = fu_fdt_firmware_get_image_by_path(FU_FDT_FIRMWARE(fdt), "/chosen/fwupd", &error);
	g_assert_no_error(error);
	g_assert_nonnull(fdt_root);
	fdt_node = fu_fdt_firmware_get_image_by_path(FU_FDT_FIRMWARE(fdt),
						     "/chosen/fwupd/firmware",
						     &error);
	g_assert_no_error(error);
	g_assert_nonnull(fdt_node);

	return g_object_new(FU_TYPE_VBE_SIMPLE_DEVICE,
			    "context",
			    ctx,
			    "fdt-root",
			    fdt_root,
			    "fdt-node",
			    fdt_node,
			    NULL);
}

static void
fu_vbe_simple_device_probe_storage_func(void)
{
	gboolean ret;
	g_autofree gchar *str = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) dev = NULL;
	g_autoptr(GError) error = NULL;

	/* the device number follows the media name */
	dev = fu_vbe_simple_test_device_new(ctx, "mmc1");
	ret = fu_device_probe(dev, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	str = fu_device_to_string(dev);
	g_assert_nonnull(g_strstr_len(str, -1, "/dev/mmcblk1"));
}

/* build a FIT with firmware for two boards, the second being the test board */
static GBytes *
fu_vbe_simple_test_fit_new(void)
{
	g_autoptr(FuFirmware) fit = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;

	fit = fu_firmware_new_from_xml(
	    "<firmware gtype=\"FuFitFirmware\">\n"
	    "  <firmware gtype=\"FuFdtImage\">\n"
	    "    <metadata key=\"timestamp\" format=\"uint32\">0x629d4abd</metadata>\n"
	    "    <firmware gtype=\"FuFdtImage\">\n"
	    "      <id>images</id>\n"
	    "      <firmware gtype=\"FuFdtImage\">\n"
	    "        <id>firmware-1</id>\n"
	    "        <metadata key=\"type\" format=\"str\">firmware</metadata>\n"
	    "        <metadata key=\"description\" format=\"str\">other</metadata>\n"
	    "        <metadata key=\"data\" format=\"data\">b3RoZXI=</metadata>\n" /* other */
	    "        <metadata key=\"store-offset\" format=\"uint32\">0x100</metadata>\n"
	    "      </firmware>\n"
	    "      <firmware gtype=\"FuFdtImage\">\n"
	    "        <id>firmware-2</id>\n"
	    "        <metadata key=\"type\" format=\"str\">firmware</metadata>\n"
	    "        <metadata key=\"description\" format=\"str\">rock</metadata>\n"
	    "        <metadata key=\"data\" format=\"data\">cm9jaw==</metadata>\n" /* rock */
	    "        <metadata key=\"store-offset\" format=\"uint32\">0x200</metadata>\n"
	    "      </firmware>\n"
	    "    </firmware>\n"
	    "    <firmware gtype=\"FuFdtImage\">\n"
	    "      <id>configurations</id>\n"
	    "      <firmware gtype=\"FuFdtImage\">\n"
	    "        <id>conf-1</id>\n"
	    "        <metadata key=\"compatible\" format=\"strlist\">other,board</metadata>\n"
	    "        <metadata key=\"version\" format=\"str\">1.2.3</metadata>\n"
	    "        <metadata key=\"firmware\" format=\"strlist\">firmware-1</metadata>\n"
	    "      </firmware>\n"
	    "      <firmware gtype=\"FuFdtImage\">\n"
	    "        <id>conf-2</id>\n"
	    "        <metadata key=\"compatible\" "
	    "format=\"strlist\">pine64,rockpro64-v2.1</metadata>\n"
	    "        <metadata key=\"version\" format=\"str\">1.2.3</metadata>\n"
	    "        <metadata key=\"firmware\" format=\"strlist\">firmware-2</metadata>\n"
	    "      </firmware>\n"
	    "    </firmware>\n"
	    "  </firmware>\n"
	    "</firmware>\n",
	    &error);
	g_assert_no_error(error);
	g_assert_nonnull(fit);
	blob = fu_firmware_write(fit, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob);
	return g_steal_pointer(&blob);
}

static void
fu_vbe_simple_device_prepare_func(void)
{
	FuFirmware *img;
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) dev = NULL;
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(FuInputStream) stream = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) imgs = NULL;

	dev = fu_vbe_simple_test_device_new(ctx, "mmc1");
	ret = fu_device_probe(dev, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* only the image in the configuration for this board is used */
	blob = fu_vbe_simple_test_fit_new();
	stream = FWUPD_INPUT_STREAM(fwupd_memory_input_stream_new_from_bytes(blob));
	firmware =
	    fu_device_prepare_firmware(dev, stream, progress, FU_FIRMWARE_PARSE_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(firmware);
	imgs = fu_firmware_get_images(firmware);
	g_assert_cmpint(imgs->len, ==, 1);
	img = g_ptr_array_index(imgs, 0);
	g_assert_cmpstr(fu_firmware_get_id(img), ==, "firmware-2");
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
	g_test_add_func("/vbe/simple/probe-storage", fu_vbe_simple_device_probe_storage_func);
	g_test_add_func("/vbe/simple/prepare", fu_vbe_simple_device_prepare_func);
	return g_test_run();
}
