/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-context-private.h"
#include "fu-plugin-private.h"
#include "fu-vbe-abrec-device.h"
#include "fu-vbe-device.h"
#include "fu-vbe-plugin.h"
#include "fu-vbe-simple-device.h"

/* vbe-simple treats area-size as the end of its area, not its size */
#define FU_VBE_SIMPLE_TEST_DISK_SIZE  0x4000
#define FU_VBE_SIMPLE_TEST_AREA_START 0x1000
#define FU_VBE_SIMPLE_TEST_AREA_SIZE  0x3000
#define FU_VBE_SIMPLE_TEST_STORE      (FU_VBE_SIMPLE_TEST_AREA_START + 0x200)

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
	g_autofree gchar *storage = NULL;
	g_autofree gchar *xml = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuPlugin) plugin = fu_plugin_new_from_gtype(fu_vbe_plugin_get_type(), ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(FuTemporaryDirectory) tmpdir_storage = NULL;
	g_autoptr(GError) error = NULL;

	fu_context_add_firmware_gtypes(ctx);

	/* the device is opened when it is added */
	tmpdir_storage = fu_temporary_directory_new("vbe-storage", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir_storage);
	storage = fu_temporary_directory_build(tmpdir_storage, "testfw", NULL);
	ret = g_file_set_contents(storage, "", 0, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	xml = g_strdup_printf(
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
	    "          <metadata key=\"storage\" format=\"str\">%s</metadata>\n"
	    "          <metadata key=\"area-start\" format=\"uint32\">0x100000</metadata>\n"
	    "          <metadata key=\"area-size\" format=\"uint32\">0x200000</metadata>\n"
	    "        </firmware>\n"
	    "      </firmware>\n"
	    "    </firmware>\n"
	    "  </firmware>\n"
	    "</firmware>\n",
	    storage);
	tmpdir = fu_vbe_test_write_fdt(ctx, xml, &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);

	ret = fu_plugin_runner_coldplug(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	devices = fu_plugin_get_devices(plugin);
	g_assert_cmpint(devices->len, ==, 1);

	dev = g_ptr_array_index(devices, 0);
	g_assert_true(FU_IS_VBE_SIMPLE_DEVICE(dev));

	/* the device has been probed, so it knows its version */
	g_assert_cmpstr(fu_device_get_version(dev), ==, "1.2.3");
}

#define FU_VBE_TEST_DISK_SIZE 0x30000
#define FU_VBE_TEST_STATE     0x1000
#define FU_VBE_TEST_SLOT_INFO 0xf000 /* within each slot */
#define FU_VBE_TEST_SLOT_A    0x10000
#define FU_VBE_TEST_SLOT_B    0x20000
#define FU_VBE_TEST_FW_SIZE   0x8000

/* flags in the state block, as written by the firmware */
#define FU_VBE_TEST_TRY_B	  (1u << 2)
#define FU_VBE_TEST_RECOVERY	  (1u << 3)
#define FU_VBE_TEST_RESULT_TRYING (1u << 4)
#define FU_VBE_TEST_RESULT_OK	  (2u << 4)
#define FU_VBE_TEST_RESULT_BAD	  (3u << 4)
#define FU_VBE_TEST_PICK_B	  (1u << 6)
#define FU_VBE_TEST_PICK_RECOVERY (2u << 6)

/* start up the plugin, as happens on each boot, returning the abrec device */
static FuDevice *
fu_vbe_abrec_test_boot(FuTemporaryDirectory *tmpdir, const gchar *booted_slot)
{
	gboolean ret;
	GPtrArray *devices;
	FuDevice *dev;
	g_autofree gchar *disk = fu_temporary_directory_build(tmpdir, "disk.img", NULL);
	g_autofree gchar *dtb = fu_temporary_directory_build(tmpdir, "system.dtb", NULL);
	g_autofree gchar *slot = NULL;
	g_autofree gchar *xml = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuFirmware) fdt = NULL;
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;
	g_autoptr(GFile) file = g_file_new_for_path(dtb);

	/* the devicetree as the firmware provides it */
	fu_context_add_firmware_gtypes(ctx);
	fu_context_set_tmpdir(ctx, FU_PATH_KIND_LOCALSTATEDIR_PKG, tmpdir);
	if (booted_slot != NULL) {
		slot = g_strdup_printf("<metadata key=\"booted-slot\" format=\"str\">%s</metadata>",
				       booted_slot);
	}
	xml = g_strdup_printf(
	    "<firmware gtype=\"FuFdtFirmware\">\n"
	    "  <firmware gtype=\"FuFdtImage\">\n"
	    "    <firmware gtype=\"FuFdtImage\">\n"
	    "      <id>chosen</id>\n"
	    "      <firmware gtype=\"FuFdtImage\">\n"
	    "        <id>fwupd</id>\n"
	    "        <metadata key=\"compatible\" format=\"str\">vendor,board</metadata>\n"
	    "        <firmware gtype=\"FuFdtImage\">\n"
	    "          <id>firmware0</id>\n"
	    "          <metadata key=\"compatible\" format=\"str\">fwupd,vbe-abrec</metadata>\n"
	    "          <metadata key=\"storage\" format=\"str\">%s</metadata>\n"
	    "          <metadata key=\"area-start\" format=\"uint32\">0x%x</metadata>\n"
	    "          <metadata key=\"area-size\" format=\"uint32\">0x10000</metadata>\n"
	    "          <metadata key=\"state-offset\" format=\"uint32\">0x0</metadata>\n"
	    "          <metadata key=\"state-size\" format=\"uint32\">0x40</metadata>\n"
	    "          <metadata key=\"slot-a-offset\" format=\"uint32\">0x%x</metadata>\n"
	    "          <metadata key=\"slot-b-offset\" format=\"uint32\">0x%x</metadata>\n"
	    "          <metadata key=\"slot-size\" format=\"uint32\">0x10000</metadata>\n"
	    "          <metadata key=\"slot-info-offset\" format=\"uint32\">0x%x</metadata>\n"
	    "          <metadata key=\"slot-info-size\" format=\"uint32\">0x1000</metadata>\n"
	    "          %s\n"
	    "        </firmware>\n"
	    "      </firmware>\n"
	    "    </firmware>\n"
	    "  </firmware>\n"
	    "</firmware>\n",
	    disk,
	    (guint)FU_VBE_TEST_STATE,
	    (guint)FU_VBE_TEST_SLOT_A,
	    (guint)FU_VBE_TEST_SLOT_B,
	    (guint)FU_VBE_TEST_SLOT_INFO,
	    slot != NULL ? slot : "");
	fdt = fu_firmware_new_from_xml(xml, &error);
	g_assert_no_error(error);
	g_assert_nonnull(fdt);
	ret = fu_firmware_write_file(fdt, file, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	plugin = fu_plugin_new_from_gtype(fu_vbe_plugin_get_type(), ctx);
	ret = fu_plugin_runner_coldplug(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	devices = fu_plugin_get_devices(plugin);
	g_assert_cmpint(devices->len, ==, 1);
	dev = g_ptr_array_index(devices, 0);
	g_assert_true(FU_IS_VBE_ABREC_DEVICE(dev));

	ret = fu_device_probe(dev, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_device_open(dev, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_device_setup(dev, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	return g_object_ref(dev);
}

static GBytes *
fu_vbe_abrec_test_read_disk(FuTemporaryDirectory *tmpdir)
{
	gboolean ret;
	gsize len = 0;
	gchar *buf = NULL;
	g_autofree gchar *disk = fu_temporary_directory_build(tmpdir, "disk.img", NULL);
	g_autoptr(GError) error = NULL;

	ret = g_file_get_contents(disk, &buf, &len, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(len, ==, FU_VBE_TEST_DISK_SIZE);
	return g_bytes_new_take(buf, len);
}

/* read the state flags, checking the header and CRC */
static guint32
fu_vbe_abrec_test_get_flags(FuTemporaryDirectory *tmpdir)
{
	g_autoptr(GBytes) disk = fu_vbe_abrec_test_read_disk(tmpdir);
	const guint8 *nvd = (const guint8 *)g_bytes_get_data(disk, NULL) + FU_VBE_TEST_STATE;

	g_assert_cmpint(nvd[1], ==, 0x61);
	g_assert_cmpint(nvd[0], ==, fu_crc8(FU_CRC_KIND_B8_STANDARD, nvd + 1, 0x3f));
	return fu_memread_uint32(nvd + 8, G_LITTLE_ENDIAN);
}

/* write the state flags, as the firmware does when it boots */
static void
fu_vbe_abrec_test_set_flags(FuTemporaryDirectory *tmpdir, guint32 flags)
{
	gboolean ret;
	g_autofree gchar *fn = fu_temporary_directory_build(tmpdir, "disk.img", NULL);
	g_autoptr(GBytes) disk = fu_vbe_abrec_test_read_disk(tmpdir);
	g_autoptr(GError) error = NULL;
	g_autofree guint8 *buf = g_memdup2(g_bytes_get_data(disk, NULL), FU_VBE_TEST_DISK_SIZE);
	guint8 *nvd = buf + FU_VBE_TEST_STATE;

	fu_memwrite_uint32(nvd + 8, flags, G_LITTLE_ENDIAN);
	nvd[0] = fu_crc8(FU_CRC_KIND_B8_STANDARD, nvd + 1, 0x3f);
	ret = g_file_set_contents(fn, (const gchar *)buf, FU_VBE_TEST_DISK_SIZE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

/* get the root node of the slot info for a slot on the disk */
static FuFdtImage *
fu_vbe_abrec_test_get_slot_info(GBytes *disk, guint32 slot)
{
	gboolean ret;
	g_autoptr(FuFirmware) fdt = fu_fdt_firmware_new();
	g_autoptr(GBytes) blob = g_bytes_new_from_bytes(disk, slot + FU_VBE_TEST_SLOT_INFO, 0x1000);
	g_autoptr(GError) error = NULL;

	ret = fu_firmware_parse_bytes(fdt, blob, 0x0, FU_FIRMWARE_PARSE_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	return FU_FDT_IMAGE(fu_firmware_get_image_by_id(fdt, NULL, NULL));
}

/* get the version recorded in the slot info for a slot on the disk */
static gchar *
fu_vbe_abrec_test_get_slot_version(GBytes *disk, guint32 slot)
{
	gchar *version = NULL;
	g_autoptr(FuFdtImage) root = fu_vbe_abrec_test_get_slot_info(disk, slot);

	g_assert_nonnull(root);
	g_assert_true(fu_fdt_image_get_attr_str(root, "version", &version, NULL));
	return version;
}

/* install an update, returning the firmware which was written */
static GBytes *
fu_vbe_abrec_test_install(FuDevice *dev, /* nocheck:name */
			  const gchar *version)
{
	gboolean ret;
	g_autofree guint8 *buf = g_malloc(FU_VBE_TEST_FW_SIZE);
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(FuInputStream) stream = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;

	/*
	 * fill the image with a pattern made from the version, so that a slot
	 * shows which update it holds and whether it was copied intact
	 */
	for (guint i = 0; i < FU_VBE_TEST_FW_SIZE; i++)
		buf[i] = (i * 7) + version[i % strlen(version)];

	/* the slot image starts with a FIT */
	fu_memwrite_uint32(buf, 0xd00dfeed, G_BIG_ENDIAN);
	blob = g_bytes_new(buf, FU_VBE_TEST_FW_SIZE);
	stream = FWUPD_INPUT_STREAM(fwupd_memory_input_stream_new_from_bytes(blob));
	firmware = fu_device_prepare_firmware(dev,
					      stream,
					      progress,
					      FU_FIRMWARE_PARSE_FLAG_CACHE_STREAM,
					      &error);
	g_assert_no_error(error);
	g_assert_nonnull(firmware);
	fu_firmware_set_version(firmware, version);
	ret = fu_device_write_firmware(dev, firmware, progress, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	return g_steal_pointer(&blob);
}

static void
fu_vbe_abrec_update_func(void)
{
	gboolean ret;
	const guint8 *buf;
	guint32 val = 0;
	g_autofree gchar *disk = NULL;
	g_autofree gchar *compat = NULL;
	g_autofree gchar *guid = fwupd_guid_hash_string("VBE\\COMPATIBLE_vendor-board");
	g_autofree gchar *version = NULL;
	g_autoptr(FuFdtImage) info = NULL;
	g_autofree gchar *zeroes = g_malloc0(FU_VBE_TEST_DISK_SIZE);
	g_autoptr(FuDevice) dev = NULL;
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GBytes) fw2 = NULL;
	g_autoptr(GBytes) data = NULL;
	g_autoptr(GError) error = NULL;

	tmpdir = fu_temporary_directory_new("vbe-abrec", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);
	disk = fu_temporary_directory_build(tmpdir, "disk.img", NULL);
	ret = g_file_set_contents(disk, zeroes, FU_VBE_TEST_DISK_SIZE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* firmware which was never updated has no version */
	dev = fu_vbe_abrec_test_boot(tmpdir, "a");
	g_assert_cmpstr(fu_device_get_version(dev), ==, "0.0.0");
	g_assert_true(fu_device_has_guid(dev, guid));
	g_assert_true(fu_device_has_flag(dev, FWUPD_DEVICE_FLAG_DUAL_IMAGE));
	g_assert_true(fu_device_has_flag(dev, FWUPD_DEVICE_FLAG_SELF_RECOVERY));

	/* an update goes to B, then the firmware is asked to try it */
	fw = fu_vbe_abrec_test_install(dev, "1.2.3");
	data = fu_vbe_abrec_test_read_disk(tmpdir);
	buf = g_bytes_get_data(data, NULL);
	g_assert_cmpmem(buf + FU_VBE_TEST_SLOT_B,
			FU_VBE_TEST_FW_SIZE,
			g_bytes_get_data(fw, NULL),
			FU_VBE_TEST_FW_SIZE);
	g_assert_cmpint(fu_vbe_abrec_test_get_flags(tmpdir), ==, FU_VBE_TEST_TRY_B | 3);

	/* the slot info for B describes the new firmware, in a form libfdt accepts */
	buf = g_bytes_get_data(data, NULL);
	g_assert_cmpint(fu_memread_uint32(buf + FU_VBE_TEST_SLOT_B + FU_VBE_TEST_SLOT_INFO + 0x14,
					  G_BIG_ENDIAN),
			==,
			17);
	g_assert_cmpint(fu_memread_uint32(buf + FU_VBE_TEST_SLOT_B + FU_VBE_TEST_SLOT_INFO + 0x18,
					  G_BIG_ENDIAN),
			<=,
			17);
	info = fu_vbe_abrec_test_get_slot_info(data, FU_VBE_TEST_SLOT_B);
	g_assert_true(fu_fdt_image_get_attr_str(info, "compatible", &compat, NULL));
	g_assert_cmpstr(compat, ==, "fwupd,vbe-slot-info");
	g_assert_true(fu_fdt_image_get_attr_u32(info, "size", &val, NULL));
	g_assert_cmpint(val, ==, FU_VBE_TEST_FW_SIZE);
	g_assert_true(fu_fdt_image_get_attr_u32(info, "crc32", &val, NULL));
	g_assert_cmpint(
	    val,
	    ==,
	    fu_crc32(FU_CRC_KIND_B32_STANDARD, g_bytes_get_data(fw, NULL), FU_VBE_TEST_FW_SIZE));
	version = fu_vbe_abrec_test_get_slot_version(data, FU_VBE_TEST_SLOT_B);
	g_assert_cmpstr(version, ==, "1.2.3");
	g_clear_pointer(&version, g_free);
	g_clear_object(&dev);
	g_clear_pointer(&data, g_bytes_unref);

	/* the firmware tries B, which reaches the OS, so the update is completed */
	fu_vbe_abrec_test_set_flags(tmpdir,
				    FU_VBE_TEST_TRY_B | 2 | FU_VBE_TEST_RESULT_TRYING |
					FU_VBE_TEST_PICK_B);
	dev = fu_vbe_abrec_test_boot(tmpdir, "b");
	g_assert_cmpstr(fu_device_get_version(dev), ==, "1.2.3");
	ret = fu_device_get_results(dev, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_null(fu_device_get_update_error(dev));
	g_assert_false(fu_device_has_problem(dev, FWUPD_DEVICE_PROBLEM_UPDATE_PENDING));
	data = fu_vbe_abrec_test_read_disk(tmpdir);
	buf = g_bytes_get_data(data, NULL);
	g_assert_cmpmem(buf + FU_VBE_TEST_SLOT_A,
			FU_VBE_TEST_FW_SIZE,
			g_bytes_get_data(fw, NULL),
			FU_VBE_TEST_FW_SIZE);
	version = fu_vbe_abrec_test_get_slot_version(data, FU_VBE_TEST_SLOT_A);
	g_assert_cmpstr(version, ==, "1.2.3");
	g_clear_pointer(&version, g_free);
	g_assert_cmpint(fu_vbe_abrec_test_get_flags(tmpdir),
			==,
			FU_VBE_TEST_RESULT_OK | FU_VBE_TEST_PICK_B);
	g_clear_object(&dev);
	g_clear_pointer(&data, g_bytes_unref);

	/* nothing more happens on the next boot */
	dev = fu_vbe_abrec_test_boot(tmpdir, "a");
	g_assert_cmpstr(fu_device_get_version(dev), ==, "1.2.3");

	/* another update never reaches the OS, so the firmware goes back to A */
	fw2 = fu_vbe_abrec_test_install(dev, "1.2.4");
	g_assert_false(g_bytes_equal(fw, fw2));
	g_clear_object(&dev);
	fu_vbe_abrec_test_set_flags(tmpdir, FU_VBE_TEST_RESULT_BAD);
	dev = fu_vbe_abrec_test_boot(tmpdir, "a");
	g_assert_cmpstr(fu_device_get_version(dev), ==, "1.2.3");
	ret = fu_device_get_results(dev, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(fu_device_get_update_state(dev), ==, FWUPD_UPDATE_STATE_FAILED);
	g_assert_cmpstr(fu_device_get_update_error(dev),
			==,
			"new firmware failed to boot, so A is still in use");
	data = fu_vbe_abrec_test_read_disk(tmpdir);
	buf = g_bytes_get_data(data, NULL);
	g_assert_cmpmem(buf + FU_VBE_TEST_SLOT_A,
			FU_VBE_TEST_FW_SIZE,
			g_bytes_get_data(fw, NULL),
			FU_VBE_TEST_FW_SIZE);
	version = fu_vbe_abrec_test_get_slot_version(data, FU_VBE_TEST_SLOT_A);
	g_assert_cmpstr(version, ==, "1.2.3");
	g_clear_pointer(&version, g_free);
	g_assert_cmpint(fu_vbe_abrec_test_get_flags(tmpdir), ==, 0);
}

static void
fu_vbe_abrec_recovery_func(void)
{
	gboolean ret;
	const guint8 *buf;
	g_autofree gchar *disk = NULL;
	g_autofree gchar *str = NULL;
	g_autofree gchar *zeroes = g_malloc0(FU_VBE_TEST_DISK_SIZE);
	g_autoptr(FuDevice) dev = NULL;
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GBytes) data = NULL;
	g_autoptr(GError) error = NULL;

	tmpdir = fu_temporary_directory_new("vbe-abrec", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);
	disk = fu_temporary_directory_build(tmpdir, "disk.img", NULL);
	ret = g_file_set_contents(disk, zeroes, FU_VBE_TEST_DISK_SIZE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* recovery is requested while an update is waiting to be tried */
	dev = fu_vbe_abrec_test_boot(tmpdir, "a");
	fw = fu_vbe_abrec_test_install(dev, "1.2.3");
	g_clear_object(&dev);
	fu_vbe_abrec_test_set_flags(tmpdir,
				    FU_VBE_TEST_TRY_B | 3 | FU_VBE_TEST_RECOVERY |
					FU_VBE_TEST_PICK_RECOVERY);

	/* booting recovery leaves the update and A alone */
	dev = fu_vbe_abrec_test_boot(tmpdir, "recovery");
	g_assert_cmpstr(fu_device_get_version(dev), ==, "0.0.0");
	g_assert_cmpint(fu_vbe_abrec_test_get_flags(tmpdir),
			==,
			FU_VBE_TEST_TRY_B | 3 | FU_VBE_TEST_RECOVERY | FU_VBE_TEST_PICK_RECOVERY);
	data = fu_vbe_abrec_test_read_disk(tmpdir);
	buf = g_bytes_get_data(data, NULL);
	g_assert_cmpmem(buf + FU_VBE_TEST_SLOT_A, FU_VBE_TEST_FW_SIZE, zeroes, FU_VBE_TEST_FW_SIZE);
	str = fu_device_to_string(dev);
	g_assert_nonnull(g_strstr_len(str, -1, "Recovery:"));
	g_assert_nonnull(g_strstr_len(str, -1, "Pick:"));

	/* so the update is neither a success nor a failure yet */
	g_assert_true(fu_device_has_problem(dev, FWUPD_DEVICE_PROBLEM_UPDATE_PENDING));
	ret = fu_device_get_results(dev, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_false(ret);
	g_clear_error(&error);
	g_clear_object(&dev);
	g_clear_pointer(&data, g_bytes_unref);

	/* neither B nor A can be loaded, so the firmware marks B bad and uses recovery */
	fu_vbe_abrec_test_set_flags(tmpdir, FU_VBE_TEST_RESULT_BAD | FU_VBE_TEST_PICK_RECOVERY);
	dev = fu_vbe_abrec_test_boot(tmpdir, "recovery");
	g_assert_cmpstr(fu_device_get_version(dev), ==, "0.0.0");
	g_assert_cmpint(fu_vbe_abrec_test_get_flags(tmpdir), ==, FU_VBE_TEST_PICK_RECOVERY);
	ret = fu_device_get_results(dev, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(fu_device_get_update_state(dev), ==, FWUPD_UPDATE_STATE_FAILED);
	g_assert_cmpstr(
	    fu_device_get_update_error(dev),
	    ==,
	    "new firmware failed to boot and A could not be loaded, so recovery is in use");
	g_assert_false(fu_device_has_problem(dev, FWUPD_DEVICE_PROBLEM_UPDATE_PENDING));

	/* an update from recovery is tried in B as usual */
	g_clear_pointer(&fw, g_bytes_unref);
	fw = fu_vbe_abrec_test_install(dev, "1.2.4");
	g_assert_cmpint(fu_vbe_abrec_test_get_flags(tmpdir),
			==,
			FU_VBE_TEST_TRY_B | 3 | FU_VBE_TEST_PICK_RECOVERY);
	g_clear_object(&dev);

	/* once B reaches the OS it is copied to A, so A works again */
	fu_vbe_abrec_test_set_flags(tmpdir,
				    FU_VBE_TEST_TRY_B | 2 | FU_VBE_TEST_RESULT_TRYING |
					FU_VBE_TEST_PICK_B);
	dev = fu_vbe_abrec_test_boot(tmpdir, "b");
	g_assert_cmpstr(fu_device_get_version(dev), ==, "1.2.4");
	data = fu_vbe_abrec_test_read_disk(tmpdir);
	buf = g_bytes_get_data(data, NULL);
	g_assert_cmpmem(buf + FU_VBE_TEST_SLOT_A,
			FU_VBE_TEST_FW_SIZE,
			g_bytes_get_data(fw, NULL),
			FU_VBE_TEST_FW_SIZE);
	g_assert_cmpint(fu_vbe_abrec_test_get_flags(tmpdir),
			==,
			FU_VBE_TEST_RESULT_OK | FU_VBE_TEST_PICK_B);
}

/* create a vbe-abrec device with the given layout, without probing it */
static FuDevice *
fu_vbe_abrec_test_device_new(FuContext *ctx,
			     guint32 slot_b,
			     guint32 slot_info_offset,
			     guint32 slot_recovery)
{
	g_autofree gchar *recovery = NULL;
	g_autofree gchar *xml = NULL;
	g_autoptr(FuFirmware) fdt = NULL;
	g_autoptr(FuFdtImage) fdt_root = NULL;
	g_autoptr(FuFdtImage) fdt_node = NULL;
	g_autoptr(GError) error = NULL;

	if (slot_recovery != 0) {
		recovery = g_strdup_printf("<metadata key=\"slot-recovery-offset\" "
					   "format=\"uint32\">0x%x</metadata>",
					   (guint)slot_recovery);
	}
	xml = g_strdup_printf(
	    "<firmware gtype=\"FuFdtFirmware\">\n"
	    "  <firmware gtype=\"FuFdtImage\">\n"
	    "    <firmware gtype=\"FuFdtImage\">\n"
	    "      <id>chosen</id>\n"
	    "      <firmware gtype=\"FuFdtImage\">\n"
	    "        <id>fwupd</id>\n"
	    "        <firmware gtype=\"FuFdtImage\">\n"
	    "          <id>firmware0</id>\n"
	    "          <metadata key=\"compatible\" format=\"str\">fwupd,vbe-abrec</metadata>\n"
	    "          <metadata key=\"storage\" format=\"str\">mmc1</metadata>\n"
	    "          <metadata key=\"area-start\" format=\"uint32\">0x1000</metadata>\n"
	    "          <metadata key=\"state-offset\" format=\"uint32\">0x0</metadata>\n"
	    "          <metadata key=\"state-size\" format=\"uint32\">0x40</metadata>\n"
	    "          <metadata key=\"slot-a-offset\" format=\"uint32\">0x10000</metadata>\n"
	    "          <metadata key=\"slot-b-offset\" format=\"uint32\">0x%x</metadata>\n"
	    "          <metadata key=\"slot-size\" format=\"uint32\">0x10000</metadata>\n"
	    "          <metadata key=\"slot-info-offset\" format=\"uint32\">0x%x</metadata>\n"
	    "          <metadata key=\"slot-info-size\" format=\"uint32\">0x1000</metadata>\n"
	    "          %s\n"
	    "        </firmware>\n"
	    "      </firmware>\n"
	    "    </firmware>\n"
	    "  </firmware>\n"
	    "</firmware>\n",
	    (guint)slot_b,
	    (guint)slot_info_offset,
	    recovery != NULL ? recovery : "");
	fu_context_add_firmware_gtypes(ctx);
	fdt = fu_firmware_new_from_xml(xml, &error);
	g_assert_no_error(error);
	g_assert_nonnull(fdt);
	fdt_root = fu_fdt_firmware_get_image_by_path(FU_FDT_FIRMWARE(fdt), "/chosen/fwupd", &error);
	g_assert_no_error(error);
	fdt_node = fu_fdt_firmware_get_image_by_path(FU_FDT_FIRMWARE(fdt),
						     "/chosen/fwupd/firmware0",
						     &error);
	g_assert_no_error(error);
	return g_object_new(FU_TYPE_VBE_ABREC_DEVICE,
			    "context",
			    ctx,
			    "fdt-root",
			    fdt_root,
			    "fdt-node",
			    fdt_node,
			    NULL);
}

static void
fu_vbe_abrec_layout_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) dev = NULL;
	g_autoptr(FuDevice) dev_overlap = NULL;
	g_autoptr(FuDevice) dev_recovery = NULL;
	g_autoptr(FuDevice) dev_recovery_overlap = NULL;
	g_autoptr(FuDevice) dev_high = NULL;
	static const guint8 big[0xf001] = {0xd0, 0x0d, 0xfe, 0xed};
	g_autoptr(FuDevice) dev_version = NULL;
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(FuInputStream) stream = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;

	/* separate slots are fine */
	dev = fu_vbe_abrec_test_device_new(ctx, 0x20000, 0xf000, 0);
	ret = fu_device_probe(dev, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* an update to B would overwrite part of A */
	dev_overlap = fu_vbe_abrec_test_device_new(ctx, 0x18000, 0xf000, 0);
	ret = fu_device_probe(dev_overlap, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_cmpstr(error->message, ==, "slot A overlaps slot B");
	g_assert_false(ret);
	g_clear_error(&error);

	/* recovery is fine after B, but an update to B must not overwrite it */
	dev_recovery = fu_vbe_abrec_test_device_new(ctx, 0x20000, 0xf000, 0x30000);
	ret = fu_device_probe(dev_recovery, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	dev_recovery_overlap = fu_vbe_abrec_test_device_new(ctx, 0x20000, 0xf000, 0x28000);
	ret = fu_device_probe(dev_recovery_overlap, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_cmpstr(error->message, ==, "slot B overlaps slot recovery");
	g_assert_false(ret);
	g_clear_error(&error);

	/* a slot ending above 4GiB would wrap around to the start of the storage */
	dev_high = fu_vbe_abrec_test_device_new(ctx, 0xffff8000, 0xf000, 0);
	ret = fu_device_probe(dev_high, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_cmpstr(error->message, ==, "slot B ends beyond 4GiB");
	g_assert_false(ret);
	g_clear_error(&error);

	/* the slot info must be inside the slot */
	dev_version = fu_vbe_abrec_test_device_new(ctx, 0x20000, 0xf800, 0);
	ret = fu_device_probe(dev_version, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_cmpstr(error->message, ==, "slot info does not fit in the slot");
	g_assert_false(ret);
	g_clear_error(&error);

	/* an image which would run into the slot info is rejected */
	blob = g_bytes_new(big, sizeof(big));
	stream = FWUPD_INPUT_STREAM(fwupd_memory_input_stream_new_from_bytes(blob));
	firmware =
	    fu_device_prepare_firmware(dev, stream, progress, FU_FIRMWARE_PARSE_FLAG_NONE, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_assert_null(firmware);
}

static void
fu_vbe_abrec_no_booted_slot_func(void)
{
	gboolean ret;
	const guint8 *buf;
	g_autofree gchar *disk = NULL;
	g_autofree gchar *zeroes = g_malloc0(FU_VBE_TEST_DISK_SIZE);
	g_autoptr(FuDevice) dev = NULL;
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GBytes) data = NULL;
	g_autoptr(GError) error = NULL;

	tmpdir = fu_temporary_directory_new("vbe-abrec", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);
	disk = fu_temporary_directory_build(tmpdir, "disk.img", NULL);
	ret = g_file_set_contents(disk, zeroes, FU_VBE_TEST_DISK_SIZE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	dev = fu_vbe_abrec_test_boot(tmpdir, "a");
	fw = fu_vbe_abrec_test_install(dev, "1.2.3");
	g_clear_object(&dev);

	/* the firmware tries B but does not say so, other than in its pick */
	fu_vbe_abrec_test_set_flags(tmpdir,
				    FU_VBE_TEST_TRY_B | 2 | FU_VBE_TEST_RESULT_TRYING |
					FU_VBE_TEST_PICK_B);
	dev = fu_vbe_abrec_test_boot(tmpdir, NULL);
	g_assert_cmpstr(fu_device_get_version(dev), ==, "1.2.3");
	data = fu_vbe_abrec_test_read_disk(tmpdir);
	buf = g_bytes_get_data(data, NULL);
	g_assert_cmpmem(buf + FU_VBE_TEST_SLOT_A,
			FU_VBE_TEST_FW_SIZE,
			g_bytes_get_data(fw, NULL),
			FU_VBE_TEST_FW_SIZE);
	g_assert_cmpint(fu_vbe_abrec_test_get_flags(tmpdir),
			==,
			FU_VBE_TEST_RESULT_OK | FU_VBE_TEST_PICK_B);
}

/* write some bytes to the disk, as a hardware failure might leave them */
static void
fu_vbe_abrec_test_write_disk(FuTemporaryDirectory *tmpdir,
			     guint32 offset,
			     const guint8 *data,
			     gsize len)
{
	gboolean ret;
	g_autofree gchar *fn = fu_temporary_directory_build(tmpdir, "disk.img", NULL);
	g_autoptr(GBytes) disk = fu_vbe_abrec_test_read_disk(tmpdir);
	g_autoptr(GError) error = NULL;
	g_autofree guint8 *buf = g_memdup2(g_bytes_get_data(disk, NULL), FU_VBE_TEST_DISK_SIZE);

	ret = fu_memcpy_safe(buf, FU_VBE_TEST_DISK_SIZE, offset, data, len, 0x0, len, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = g_file_set_contents(fn, (const gchar *)buf, FU_VBE_TEST_DISK_SIZE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

/* write a devicetree where A's slot info goes, with the given properties */
static void
fu_vbe_abrec_test_set_slot_info(FuTemporaryDirectory *tmpdir,
				const gchar *compatible,
				const gchar *version)
{
	gboolean ret;
	g_autofree gchar *fn = fu_temporary_directory_build(tmpdir, "disk.img", NULL);
	g_autoptr(FuFirmware) fdt = fu_fdt_firmware_new();
	g_autoptr(FuFirmware) root = fu_fdt_image_new();
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GBytes) disk = fu_vbe_abrec_test_read_disk(tmpdir);
	g_autoptr(GError) error = NULL;
	g_autofree guint8 *buf = g_memdup2(g_bytes_get_data(disk, NULL), FU_VBE_TEST_DISK_SIZE);

	if (compatible != NULL)
		fu_fdt_image_set_attr_str(FU_FDT_IMAGE(root), "compatible", compatible);
	fu_fdt_image_set_attr_str(FU_FDT_IMAGE(root), "version", version);
	ret = fu_firmware_add_image(fdt, root, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	blob = fu_firmware_write(fdt, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob);
	ret = fu_memcpy_safe(buf,
			     FU_VBE_TEST_DISK_SIZE,
			     FU_VBE_TEST_SLOT_A + FU_VBE_TEST_SLOT_INFO,
			     g_bytes_get_data(blob, NULL),
			     g_bytes_get_size(blob),
			     0x0,
			     g_bytes_get_size(blob),
			     &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = g_file_set_contents(fn, (const gchar *)buf, FU_VBE_TEST_DISK_SIZE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_vbe_abrec_slot_info_compat_func(void)
{
	static const guint8 junk[0x40] = {0xd0, 0x0d, 0xfe, 0xed, 0x00, 0x00, 0x00, 0x40, 0x12};
	gboolean ret;
	g_autofree gchar *disk = NULL;
	g_autofree gchar *zeroes = g_malloc0(FU_VBE_TEST_DISK_SIZE);
	g_autoptr(FuDevice) dev = NULL;
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(GError) error = NULL;

	tmpdir = fu_temporary_directory_new("vbe-abrec", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);
	disk = fu_temporary_directory_build(tmpdir, "disk.img", NULL);
	ret = g_file_set_contents(disk, zeroes, FU_VBE_TEST_DISK_SIZE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* a devicetree which is not slot info gives no version */
	fu_vbe_abrec_test_set_slot_info(tmpdir, NULL, "9.9.9");
	dev = fu_vbe_abrec_test_boot(tmpdir, "a");
	g_assert_cmpstr(fu_device_get_version(dev), ==, "0.0.0");
	g_clear_object(&dev);

	/* a devicetree header followed by rubbish, e.g. from a torn write */
	fu_vbe_abrec_test_write_disk(tmpdir,
				     FU_VBE_TEST_SLOT_A + FU_VBE_TEST_SLOT_INFO,
				     junk,
				     sizeof(junk));
	dev = fu_vbe_abrec_test_boot(tmpdir, "a");
	g_assert_cmpstr(fu_device_get_version(dev), ==, "0.0.0");
	g_clear_object(&dev);

	/* slot info has the compatible string */
	fu_vbe_abrec_test_set_slot_info(tmpdir, "fwupd,vbe-slot-info", "9.9.9");
	dev = fu_vbe_abrec_test_boot(tmpdir, "a");
	g_assert_cmpstr(fu_device_get_version(dev), ==, "9.9.9");
}

static void
fu_vbe_abrec_short_state_func(void)
{
	gboolean ret;
	g_autofree gchar *disk = NULL;
	g_autofree guint8 *raw = g_malloc0(FU_VBE_TEST_DISK_SIZE);
	g_autoptr(FuDevice) dev = NULL;
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(GError) error = NULL;

	tmpdir = fu_temporary_directory_new("vbe-abrec", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);

	/*
	 * the header gives a size of one byte, whose CRC8 is zero, so the CRC
	 * matches, but the flags are not covered by it, so this is not valid
	 */
	raw[FU_VBE_TEST_STATE + 1] = 0x01;
	fu_memwrite_uint32(raw + FU_VBE_TEST_STATE + 8, FU_VBE_TEST_TRY_B | 3, G_LITTLE_ENDIAN);
	disk = fu_temporary_directory_build(tmpdir, "disk.img", NULL);
	ret = g_file_set_contents(disk, (const gchar *)raw, FU_VBE_TEST_DISK_SIZE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	dev = fu_vbe_abrec_test_boot(tmpdir, "a");
	g_assert_false(fu_device_has_problem(dev, FWUPD_DEVICE_PROBLEM_UPDATE_PENDING));
}

/* set up a disk with an update installed in B, returning the update */
static GBytes *
fu_vbe_abrec_test_setup_update(FuTemporaryDirectory *tmpdir)
{
	gboolean ret;
	g_autofree gchar *disk = fu_temporary_directory_build(tmpdir, "disk.img", NULL);
	g_autofree gchar *zeroes = g_malloc0(FU_VBE_TEST_DISK_SIZE);
	g_autoptr(FuDevice) dev = NULL;
	g_autoptr(GError) error = NULL;

	ret = g_file_set_contents(disk, zeroes, FU_VBE_TEST_DISK_SIZE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	dev = fu_vbe_abrec_test_boot(tmpdir, "a");
	return fu_vbe_abrec_test_install(dev, "1.2.3");
}

static void
fu_vbe_abrec_torn_state_func(void)
{
	static const guint8 torn[] = {0xff, 0xff, 0xff, 0xff};
	g_autoptr(FuDevice) dev = NULL;
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GError) error = NULL;

	tmpdir = fu_temporary_directory_new("vbe-abrec", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);
	fw = fu_vbe_abrec_test_setup_update(tmpdir);

	/*
	 * power fails while the state is written, so its CRC is wrong; the
	 * firmware then starts with empty state and never tries B, so the
	 * update is lost, but A is still used and another update can be
	 * installed
	 */
	fu_vbe_abrec_test_write_disk(tmpdir, FU_VBE_TEST_STATE + 8, torn, sizeof(torn));
	dev = fu_vbe_abrec_test_boot(tmpdir, "a");
	g_assert_false(fu_device_has_problem(dev, FWUPD_DEVICE_PROBLEM_UPDATE_PENDING));
	g_assert_cmpstr(fu_device_get_version(dev), ==, "0.0.0");
	g_clear_pointer(&fw, g_bytes_unref);
	fw = fu_vbe_abrec_test_install(dev, "1.2.4");
	g_assert_cmpint(fu_vbe_abrec_test_get_flags(tmpdir), ==, FU_VBE_TEST_TRY_B | 3);
}

static void
fu_vbe_abrec_interrupted_copy_func(void)
{
	gboolean ret;
	const guint8 *buf;
	g_autofree guint8 *half = g_malloc0(FU_VBE_TEST_FW_SIZE);
	g_autoptr(FuDevice) dev = NULL;
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(GBytes) data = NULL;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GError) error = NULL;

	tmpdir = fu_temporary_directory_new("vbe-abrec", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);
	fw = fu_vbe_abrec_test_setup_update(tmpdir);

	/*
	 * power fails while B is copied to A, so A holds part of each; the
	 * state still asks for B, so the firmware boots it again, here on its
	 * last try, and the copy is done again
	 */
	ret = fu_memcpy_safe(half,
			     FU_VBE_TEST_FW_SIZE,
			     0x0,
			     g_bytes_get_data(fw, NULL),
			     FU_VBE_TEST_FW_SIZE,
			     0x0,
			     FU_VBE_TEST_FW_SIZE / 2,
			     &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	fu_vbe_abrec_test_write_disk(tmpdir, FU_VBE_TEST_SLOT_A, half, FU_VBE_TEST_FW_SIZE);
	fu_vbe_abrec_test_set_flags(tmpdir,
				    FU_VBE_TEST_TRY_B | 0 | FU_VBE_TEST_RESULT_TRYING |
					FU_VBE_TEST_PICK_B);
	dev = fu_vbe_abrec_test_boot(tmpdir, "b");
	g_assert_cmpstr(fu_device_get_version(dev), ==, "1.2.3");
	g_assert_false(fu_device_has_problem(dev, FWUPD_DEVICE_PROBLEM_UPDATE_PENDING));
	data = fu_vbe_abrec_test_read_disk(tmpdir);
	buf = g_bytes_get_data(data, NULL);
	g_assert_cmpmem(buf + FU_VBE_TEST_SLOT_A,
			FU_VBE_TEST_FW_SIZE,
			g_bytes_get_data(fw, NULL),
			FU_VBE_TEST_FW_SIZE);
	g_assert_cmpint(fu_vbe_abrec_test_get_flags(tmpdir),
			==,
			FU_VBE_TEST_RESULT_OK | FU_VBE_TEST_PICK_B);
}

static void
fu_vbe_abrec_copy_fails_func(void)
{
	gboolean ret;
	guint32 flags = FU_VBE_TEST_TRY_B | 2 | FU_VBE_TEST_RESULT_TRYING | FU_VBE_TEST_PICK_B;
	g_autofree gchar *disk = NULL;
	g_autoptr(FuDevice) dev = NULL;
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(GBytes) data = NULL;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GError) error = NULL;

	tmpdir = fu_temporary_directory_new("vbe-abrec", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);
	fw = fu_vbe_abrec_test_setup_update(tmpdir);
	g_assert_nonnull(fw);
	fu_vbe_abrec_test_set_flags(tmpdir, flags);

	/* B cannot be read, so it cannot be copied to A */
	data = fu_vbe_abrec_test_read_disk(tmpdir);
	disk = fu_temporary_directory_build(tmpdir, "disk.img", NULL);
	ret = g_file_set_contents(disk,
				  g_bytes_get_data(data, NULL),
				  FU_VBE_TEST_SLOT_B + FU_VBE_TEST_FW_SIZE,
				  &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* the device stays, with the update still pending and the reason given */
	dev = fu_vbe_abrec_test_boot(tmpdir, "b");
	g_assert_true(fu_device_has_problem(dev, FWUPD_DEVICE_PROBLEM_UPDATE_PENDING));
	ret = fu_device_get_results(dev, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(fu_device_get_update_state(dev), ==, FWUPD_UPDATE_STATE_FAILED);
	g_assert_cmpstr(fu_device_get_update_error(dev),
			==,
			"new firmware booted but could not be copied to A");
	g_clear_object(&dev);

	/* the state is unchanged, so once B can be read, the next boot copies it */
	ret =
	    g_file_set_contents(disk, g_bytes_get_data(data, NULL), FU_VBE_TEST_DISK_SIZE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(fu_vbe_abrec_test_get_flags(tmpdir), ==, flags);
	dev = fu_vbe_abrec_test_boot(tmpdir, "b");
	g_assert_cmpstr(fu_device_get_version(dev), ==, "1.2.3");
	g_assert_cmpint(fu_vbe_abrec_test_get_flags(tmpdir),
			==,
			FU_VBE_TEST_RESULT_OK | FU_VBE_TEST_PICK_B);
}

static void
fu_vbe_abrec_write_fails_func(void)
{
	gboolean ret;
	g_autofree gchar *disk = NULL;
	g_autofree guint8 *buf = g_malloc0(FU_VBE_TEST_FW_SIZE);
	g_autoptr(FuDevice) dev = NULL;
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(FuInputStream) stream = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GFile) link = NULL;

	/* /dev/full reads as zeroes, but every write fails */
	if (!g_file_test("/dev/full", G_FILE_TEST_EXISTS)) {
		g_test_skip("no /dev/full");
		return;
	}
	tmpdir = fu_temporary_directory_new("vbe-abrec", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);
	disk = fu_temporary_directory_build(tmpdir, "disk.img", NULL);
	link = g_file_new_for_path(disk);
	ret = g_file_make_symbolic_link(link, "/dev/full", NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	dev = fu_vbe_abrec_test_boot(tmpdir, "a");

	/* the update cannot be written, so it fails */
	fu_memwrite_uint32(buf, 0xd00dfeed, G_BIG_ENDIAN);
	blob = g_bytes_new(buf, FU_VBE_TEST_FW_SIZE);
	stream = FWUPD_INPUT_STREAM(fwupd_memory_input_stream_new_from_bytes(blob));
	firmware = fu_device_prepare_firmware(dev,
					      stream,
					      progress,
					      FU_FIRMWARE_PARSE_FLAG_CACHE_STREAM,
					      &error);
	g_assert_no_error(error);
	g_assert_nonnull(firmware);
	ret = fu_device_write_firmware(dev, firmware, progress, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_nonnull(error);
	g_assert_true(g_str_has_prefix(error->message, "failed to write B at 0x20000: "));
	g_assert_false(ret);
}

static void
fu_vbe_abrec_state_fields_func(void)
{
	gboolean ret;
	const guint8 *buf;
	g_autofree gchar *disk = NULL;
	g_autofree gchar *zeroes = g_malloc0(FU_VBE_TEST_DISK_SIZE);
	g_autofree guint8 *raw = NULL;
	g_autoptr(FuDevice) dev = NULL;
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GBytes) data = NULL;
	g_autoptr(GError) error = NULL;

	tmpdir = fu_temporary_directory_new("vbe-abrec", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);
	disk = fu_temporary_directory_build(tmpdir, "disk.img", NULL);
	ret = g_file_set_contents(disk, zeroes, FU_VBE_TEST_DISK_SIZE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	dev = fu_vbe_abrec_test_boot(tmpdir, "a");
	fw = fu_vbe_abrec_test_install(dev, "1.2.3");
	g_assert_nonnull(fw);
	g_clear_object(&dev);

	/* the firmware keeps something of its own in the state */
	data = fu_vbe_abrec_test_read_disk(tmpdir);
	raw = g_memdup2(g_bytes_get_data(data, NULL), FU_VBE_TEST_DISK_SIZE);
	raw[FU_VBE_TEST_STATE + 0x20] = 0xab;
	ret = g_file_set_contents(disk, (const gchar *)raw, FU_VBE_TEST_DISK_SIZE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_clear_pointer(&data, g_bytes_unref);
	fu_vbe_abrec_test_set_flags(tmpdir,
				    FU_VBE_TEST_TRY_B | 2 | FU_VBE_TEST_RESULT_TRYING |
					FU_VBE_TEST_PICK_B);

	/* completing the update rewrites the state, but leaves that alone */
	dev = fu_vbe_abrec_test_boot(tmpdir, "b");
	g_assert_cmpint(fu_vbe_abrec_test_get_flags(tmpdir),
			==,
			FU_VBE_TEST_RESULT_OK | FU_VBE_TEST_PICK_B);
	data = fu_vbe_abrec_test_read_disk(tmpdir);
	buf = g_bytes_get_data(data, NULL);
	g_assert_cmpint(buf[FU_VBE_TEST_STATE + 0x20], ==, 0xab);
}

static void
fu_vbe_device_parse_devnum_func(void)
{
	guint devnum = 0;
	g_autoptr(GError) error = NULL;

	g_assert_true(fu_vbe_device_parse_devnum("mmc1", &devnum, &error));
	g_assert_no_error(error);
	g_assert_cmpint(devnum, ==, 1);
	g_assert_true(fu_vbe_device_parse_devnum("mmc12", &devnum, &error));
	g_assert_no_error(error);
	g_assert_cmpint(devnum, ==, 12);
	g_assert_false(fu_vbe_device_parse_devnum("mmc", &devnum, &error));
	g_assert_nonnull(error);
}

static void
fu_vbe_plugin_coldplug_two_func(void)
{
	gboolean ret;
	GPtrArray *devices;
	g_autofree gchar *storage = NULL;
	g_autofree gchar *xml = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuPlugin) plugin = fu_plugin_new_from_gtype(fu_vbe_plugin_get_type(), ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(FuTemporaryDirectory) tmpdir_storage = NULL;
	g_autoptr(GError) error = NULL;

	fu_context_add_firmware_gtypes(ctx);
	tmpdir_storage = fu_temporary_directory_new("vbe-storage", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir_storage);
	storage = fu_temporary_directory_build(tmpdir_storage, "testfw", NULL);
	ret = g_file_set_contents(storage, "", 0, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* two methods, each with its own node */
	xml = g_strdup_printf(
	    "<firmware gtype=\"FuFdtFirmware\">\n"
	    "  <firmware gtype=\"FuFdtImage\">\n"
	    "    <firmware gtype=\"FuFdtImage\">\n"
	    "      <id>chosen</id>\n"
	    "      <firmware gtype=\"FuFdtImage\">\n"
	    "        <id>fwupd</id>\n"
	    "        <firmware gtype=\"FuFdtImage\">\n"
	    "          <id>firmware0</id>\n"
	    "          <metadata key=\"compatible\" format=\"str\">fwupd,vbe-simple</metadata>\n"
	    "          <metadata key=\"storage\" format=\"str\">%s</metadata>\n"
	    "          <metadata key=\"area-start\" format=\"uint32\">0x100000</metadata>\n"
	    "          <metadata key=\"area-size\" format=\"uint32\">0x200000</metadata>\n"
	    "        </firmware>\n"
	    "        <firmware gtype=\"FuFdtImage\">\n"
	    "          <id>firmware1</id>\n"
	    "          <metadata key=\"compatible\" format=\"str\">fwupd,vbe-simple</metadata>\n"
	    "          <metadata key=\"storage\" format=\"str\">%s</metadata>\n"
	    "          <metadata key=\"area-start\" format=\"uint32\">0x300000</metadata>\n"
	    "          <metadata key=\"area-size\" format=\"uint32\">0x400000</metadata>\n"
	    "        </firmware>\n"
	    "      </firmware>\n"
	    "    </firmware>\n"
	    "  </firmware>\n"
	    "</firmware>\n",
	    storage,
	    storage);
	tmpdir = fu_vbe_test_write_fdt(ctx, xml, &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);
	ret = fu_plugin_runner_coldplug(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* both are kept, rather than one replacing the other */
	devices = fu_plugin_get_devices(plugin);
	g_assert_cmpint(devices->len, ==, 2);
	g_assert_cmpstr(fu_device_get_id(g_ptr_array_index(devices, 0)),
			!=,
			fu_device_get_id(g_ptr_array_index(devices, 1)));
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

	/* updates can be for every vbe-simple board, or just this one */
	g_assert_true(fu_device_has_instance_id(dev,
						"bb3b05a8-ebef-11ec-be98-d3a15278be95",
						FU_DEVICE_INSTANCE_FLAG_VISIBLE));
	g_assert_true(fu_device_has_instance_id(dev,
						"VBE\\COMPATIBLE_pine64-rockpro64-v2.1",
						FU_DEVICE_INSTANCE_FLAG_VISIBLE));
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

static GBytes *
fu_vbe_test_fit_new_cfgs(const gchar *cfgs)
{
	g_autofree gchar *xml = NULL;
	g_autoptr(FuFirmware) fit = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;

	xml = g_strdup_printf(
	    "<firmware gtype=\"FuFitFirmware\">\n"
	    "  <firmware gtype=\"FuFdtImage\">\n"
	    "    <metadata key=\"timestamp\" format=\"uint32\">0x629d4abd</metadata>\n"
	    "    <firmware gtype=\"FuFdtImage\">\n"
	    "      <id>images</id>\n"
	    "      <firmware gtype=\"FuFdtImage\">\n"
	    "        <id>firmware-1</id>\n"
	    "        <metadata key=\"type\" format=\"str\">firmware</metadata>\n"
	    "        <metadata key=\"description\" format=\"str\">any</metadata>\n"
	    "        <metadata key=\"data\" format=\"data\">YW55</metadata>\n" /* any */
	    "      </firmware>\n"
	    "      <firmware gtype=\"FuFdtImage\">\n"
	    "        <id>firmware-2</id>\n"
	    "        <metadata key=\"type\" format=\"str\">firmware</metadata>\n"
	    "        <metadata key=\"description\" format=\"str\">rock</metadata>\n"
	    "        <metadata key=\"data\" format=\"data\">cm9jaw==</metadata>\n" /* rock */
	    "      </firmware>\n"
	    "    </firmware>\n"
	    "    <firmware gtype=\"FuFdtImage\">\n"
	    "      <id>configurations</id>\n"
	    "%s"
	    "    </firmware>\n"
	    "  </firmware>\n"
	    "</firmware>\n",
	    cfgs);
	fit = fu_firmware_new_from_xml(xml, &error);
	g_assert_no_error(error);
	g_assert_nonnull(fit);
	blob = fu_firmware_write(fit, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob);
	return g_steal_pointer(&blob);
}

/* get the ID of the image which vbe-simple picks from a FIT with these configurations */
static gchar *
fu_vbe_test_pick_image(const gchar *cfgs, FuDevice *device, GError **error)
{
	FuFirmware *img;
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(FuInputStream) stream = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GBytes) blob = fu_vbe_test_fit_new_cfgs(cfgs);
	g_autoptr(GPtrArray) imgs = NULL;

	stream = FWUPD_INPUT_STREAM(fwupd_memory_input_stream_new_from_bytes(blob));
	firmware = fu_device_prepare_firmware(device,
					      stream,
					      progress,
					      FU_FIRMWARE_PARSE_FLAG_NONE,
					      error);
	if (firmware == NULL)
		return NULL;
	imgs = fu_firmware_get_images(firmware);
	g_assert_cmpint(imgs->len, ==, 1);
	img = g_ptr_array_index(imgs, 0);
	return g_strdup(fu_firmware_get_id(img));
}

static void
fu_vbe_simple_device_no_compatible_func(void)
{
	gboolean ret;
	g_autofree gchar *id_any = NULL;
	g_autofree gchar *id_skip = NULL;
	g_autofree gchar *id_none = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) dev = NULL;
	g_autoptr(GError) error = NULL;

	dev = fu_vbe_simple_test_device_new(ctx, "mmc1");
	ret = fu_device_probe(dev, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* a single configuration with no compatible strings applies to every board */
	id_any = fu_vbe_test_pick_image(
	    "      <firmware gtype=\"FuFdtImage\">\n"
	    "        <id>conf-1</id>\n"
	    "        <metadata key=\"version\" format=\"str\">1.2.3</metadata>\n"
	    "        <metadata key=\"firmware\" format=\"strlist\">firmware-1</metadata>\n"
	    "      </firmware>\n",
	    dev,
	    &error);
	g_assert_no_error(error);
	g_assert_cmpstr(id_any, ==, "firmware-1");

	/* with more than one, a configuration with no compatible strings is skipped */
	id_skip = fu_vbe_test_pick_image(
	    "      <firmware gtype=\"FuFdtImage\">\n"
	    "        <id>conf-1</id>\n"
	    "        <metadata key=\"version\" format=\"str\">1.2.3</metadata>\n"
	    "        <metadata key=\"firmware\" format=\"strlist\">firmware-1</metadata>\n"
	    "      </firmware>\n"
	    "      <firmware gtype=\"FuFdtImage\">\n"
	    "        <id>conf-2</id>\n"
	    "        <metadata key=\"compatible\" "
	    "format=\"strlist\">pine64,rockpro64-v2.1</metadata>\n"
	    "        <metadata key=\"version\" format=\"str\">1.2.3</metadata>\n"
	    "        <metadata key=\"firmware\" format=\"strlist\">firmware-2</metadata>\n"
	    "      </firmware>\n",
	    dev,
	    &error);
	g_assert_no_error(error);
	g_assert_cmpstr(id_skip, ==, "firmware-2");

	/* ...so if none of the others match, there is nothing for this board */
	id_none = fu_vbe_test_pick_image(
	    "      <firmware gtype=\"FuFdtImage\">\n"
	    "        <id>conf-1</id>\n"
	    "        <metadata key=\"version\" format=\"str\">1.2.3</metadata>\n"
	    "        <metadata key=\"firmware\" format=\"strlist\">firmware-1</metadata>\n"
	    "      </firmware>\n"
	    "      <firmware gtype=\"FuFdtImage\">\n"
	    "        <id>conf-2</id>\n"
	    "        <metadata key=\"compatible\" format=\"strlist\">other,board</metadata>\n"
	    "        <metadata key=\"version\" format=\"str\">1.2.3</metadata>\n"
	    "        <metadata key=\"firmware\" format=\"strlist\">firmware-2</metadata>\n"
	    "      </firmware>\n",
	    dev,
	    &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_null(id_none);
}

static void
fu_vbe_simple_device_external_func(void)
{
	gboolean ret;
	gsize len = 0;
	g_autofree gchar *contents = NULL;
	g_autofree gchar *disk = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) dev = NULL;
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(FuFirmware) fit = NULL;
	g_autoptr(FuInputStream) stream = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GBytes) fdt = NULL;
	g_autoptr(GError) error = NULL;

	/* a file standing in for the storage device */
	tmpdir = fu_temporary_directory_new("vbe-simple", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);
	disk = fu_temporary_directory_build(tmpdir, "disk.img", NULL);
	contents = g_malloc0(FU_VBE_SIMPLE_TEST_DISK_SIZE);
	ret = g_file_set_contents(disk, contents, FU_VBE_SIMPLE_TEST_DISK_SIZE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_clear_pointer(&contents, g_free);
	dev = fu_vbe_simple_test_device_new(ctx, disk);
	ret = fu_device_probe(dev, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_device_open(dev, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* an update with its data after the FIT, as written by 'mkimage -E' */
	fit = fu_firmware_new_from_xml(
	    "<firmware gtype=\"FuFitFirmware\">\n"
	    "  <firmware gtype=\"FuFdtImage\">\n"
	    "    <metadata key=\"timestamp\" format=\"uint32\">0x629d4abd</metadata>\n"
	    "    <firmware gtype=\"FuFdtImage\">\n"
	    "      <id>images</id>\n"
	    "      <firmware gtype=\"FuFdtImage\">\n"
	    "        <id>firmware-1</id>\n"
	    "        <metadata key=\"type\" format=\"str\">firmware</metadata>\n"
	    "        <metadata key=\"description\" format=\"str\">rock</metadata>\n"
	    "        <metadata key=\"data-offset\" format=\"uint32\">0x0</metadata>\n"
	    "        <metadata key=\"data-size\" format=\"uint32\">0x4</metadata>\n"
	    "        <metadata key=\"store-offset\" format=\"uint32\">0x200</metadata>\n"
	    "      </firmware>\n"
	    "    </firmware>\n"
	    "    <firmware gtype=\"FuFdtImage\">\n"
	    "      <id>configurations</id>\n"
	    "      <firmware gtype=\"FuFdtImage\">\n"
	    "        <id>conf-1</id>\n"
	    "        <metadata key=\"compatible\" "
	    "format=\"strlist\">pine64,rockpro64-v2.1</metadata>\n"
	    "        <metadata key=\"version\" format=\"str\">1.2.3</metadata>\n"
	    "        <metadata key=\"firmware\" format=\"strlist\">firmware-1</metadata>\n"
	    "      </firmware>\n"
	    "    </firmware>\n"
	    "  </firmware>\n"
	    "</firmware>\n",
	    &error);
	g_assert_no_error(error);
	g_assert_nonnull(fit);
	fdt = fu_firmware_write(fit, &error);
	g_assert_no_error(error);
	g_assert_nonnull(fdt);
	fu_byte_array_append_bytes(buf, fdt);
	fu_byte_array_align_up(buf, FU_FIRMWARE_ALIGNMENT_4, 0x0);
	g_byte_array_append(buf, (const guint8 *)"rock", 4);
	blob = g_bytes_new(buf->data, buf->len);

	/* the data is written at the store offset */
	stream = FWUPD_INPUT_STREAM(fwupd_memory_input_stream_new_from_bytes(blob));
	firmware =
	    fu_device_prepare_firmware(dev, stream, progress, FU_FIRMWARE_PARSE_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(firmware);
	ret = fu_device_write_firmware(dev, firmware, progress, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_device_close(dev, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = g_file_get_contents(disk, &contents, &len, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(len, ==, FU_VBE_SIMPLE_TEST_DISK_SIZE);
	g_assert_cmpmem(contents + FU_VBE_SIMPLE_TEST_STORE, 4, "rock", 4);
}

static void
fu_vbe_simple_device_storage_func(void)
{
	gboolean ret;
	const guint8 *buf;
	gsize len = 0;
	g_autofree gchar *disk = NULL;
	g_autofree gchar *str = NULL;
	g_autofree guint8 *orig = g_malloc(FU_VBE_SIMPLE_TEST_DISK_SIZE);
	g_autofree gchar *contents = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) dev = NULL;
	g_autoptr(FuDevice) dev_bad = NULL;
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(FuInputStream) stream = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GBytes) fit_blob = NULL;
	g_autoptr(GError) error = NULL;

	/* a file standing in for the storage device, with a pattern in it */
	tmpdir = fu_temporary_directory_new("vbe-simple", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);
	disk = fu_temporary_directory_build(tmpdir, "disk.img", NULL);
	for (guint i = 0; i < FU_VBE_SIMPLE_TEST_DISK_SIZE; i++)
		orig[i] = i & 0xff;
	ret = g_file_set_contents(disk, (const gchar *)orig, FU_VBE_SIMPLE_TEST_DISK_SIZE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* an absolute path is used as it is */
	dev = fu_vbe_simple_test_device_new(ctx, disk);
	ret = fu_device_probe(dev, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	str = fu_device_to_string(dev);
	g_assert_nonnull(g_strstr_len(str, -1, disk));
	ret = fu_device_open(dev, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* reading gives the firmware area */
	blob = fu_device_dump_firmware(dev, progress, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob);
	buf = g_bytes_get_data(blob, &len);
	g_assert_cmpmem(buf,
			len,
			orig + FU_VBE_SIMPLE_TEST_AREA_START,
			FU_VBE_SIMPLE_TEST_AREA_SIZE - FU_VBE_SIMPLE_TEST_AREA_START);

	/* writing puts the image for this board at its store offset */
	fit_blob = fu_vbe_simple_test_fit_new();
	stream = FWUPD_INPUT_STREAM(fwupd_memory_input_stream_new_from_bytes(fit_blob));
	firmware =
	    fu_device_prepare_firmware(dev, stream, progress, FU_FIRMWARE_PARSE_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(firmware);
	ret = fu_device_write_firmware(dev, firmware, progress, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_device_close(dev, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = g_file_get_contents(disk, &contents, &len, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(len, ==, FU_VBE_SIMPLE_TEST_DISK_SIZE);
	g_assert_cmpmem(contents, FU_VBE_SIMPLE_TEST_STORE, orig, FU_VBE_SIMPLE_TEST_STORE);
	g_assert_cmpmem(contents + FU_VBE_SIMPLE_TEST_STORE, 4, "rock", 4);
	g_assert_cmpmem(contents + FU_VBE_SIMPLE_TEST_STORE + 4,
			FU_VBE_SIMPLE_TEST_DISK_SIZE - FU_VBE_SIMPLE_TEST_STORE - 4,
			orig + FU_VBE_SIMPLE_TEST_STORE + 4,
			FU_VBE_SIMPLE_TEST_DISK_SIZE - FU_VBE_SIMPLE_TEST_STORE - 4);

	/* only MMC is supported */
	dev_bad = fu_vbe_simple_test_device_new(ctx, "usb0");
	ret = fu_device_probe(dev_bad, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_false(ret);
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/vbe/device/parse-devnum", fu_vbe_device_parse_devnum_func);
	g_test_add_func("/vbe/abrec/update", fu_vbe_abrec_update_func);
	g_test_add_func("/vbe/abrec/recovery", fu_vbe_abrec_recovery_func);
	g_test_add_func("/vbe/abrec/layout", fu_vbe_abrec_layout_func);
	g_test_add_func("/vbe/abrec/no-booted-slot", fu_vbe_abrec_no_booted_slot_func);
	g_test_add_func("/vbe/abrec/slot-info-compat", fu_vbe_abrec_slot_info_compat_func);
	g_test_add_func("/vbe/abrec/short-state", fu_vbe_abrec_short_state_func);
	g_test_add_func("/vbe/abrec/torn-state", fu_vbe_abrec_torn_state_func);
	g_test_add_func("/vbe/abrec/interrupted-copy", fu_vbe_abrec_interrupted_copy_func);
	g_test_add_func("/vbe/abrec/copy-fails", fu_vbe_abrec_copy_fails_func);
	g_test_add_func("/vbe/abrec/write-fails", fu_vbe_abrec_write_fails_func);
	g_test_add_func("/vbe/abrec/state-fields", fu_vbe_abrec_state_fields_func);
	g_test_add_func("/vbe/coldplug", fu_vbe_plugin_coldplug_func);
	g_test_add_func("/vbe/coldplug/two", fu_vbe_plugin_coldplug_two_func);
	g_test_add_func("/vbe/coldplug/no-fdt", fu_vbe_plugin_coldplug_no_fdt_func);
	g_test_add_func("/vbe/coldplug/invalid-compatible",
			fu_vbe_plugin_coldplug_invalid_compatible_func);
	g_test_add_func("/vbe/simple/probe-storage", fu_vbe_simple_device_probe_storage_func);
	g_test_add_func("/vbe/simple/prepare", fu_vbe_simple_device_prepare_func);
	g_test_add_func("/vbe/simple/storage", fu_vbe_simple_device_storage_func);
	g_test_add_func("/vbe/simple/external", fu_vbe_simple_device_external_func);
	g_test_add_func("/vbe/simple/no-compatible", fu_vbe_simple_device_no_compatible_func);
	return g_test_run();
}
