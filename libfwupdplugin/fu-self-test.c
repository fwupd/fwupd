/*
 * Copyright 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include <glib/gstdio.h>
#include <string.h>

#include "fwupd-enums-private.h"
#include "fwupd-security-attr-private.h"

#include "fu-bios-settings-private.h"
#include "fu-cab-firmware-private.h"
#include "fu-config-private.h"
#include "fu-context-private.h"
#include "fu-device-event-private.h"
#include "fu-device-private.h"
#include "fu-device-progress.h"
#include "fu-dummy-efivars.h"
#include "fu-efi-lz77-decompressor.h"
#include "fu-efi-x509-signature-private.h"
#include "fu-efivars-private.h"
#include "fu-kernel-search-path-private.h"
#include "fu-plugin-private.h"
#include "fu-progress-private.h"
#include "fu-security-attrs-private.h"
#include "fu-self-test-device.h"
#include "fu-self-test-struct.h"
#include "fu-smbios-private.h"
#include "fu-test.h"
#include "fu-udev-device-private.h"
#include "fu-volume-private.h"

/* nocheck:magic-inlines=300 */

static void
fu_archive_invalid_func(void)
{
	g_autofree gchar *filename = NULL;
	g_autoptr(FuArchive) archive = NULL;
	g_autoptr(GBytes) data = NULL;
	g_autoptr(GError) error = NULL;

#ifndef HAVE_LIBARCHIVE
	g_test_skip("no libarchive support");
	return;
#endif

	filename = g_test_build_filename(G_TEST_DIST, "tests", "metadata.xml", NULL);
	data = fu_bytes_get_contents(filename, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data);

	archive = fu_archive_new(data, FU_FIRMWARE_PARSE_FLAG_NONE, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_null(archive);
}

static void
fu_archive_cab_func(void)
{
	g_autofree gchar *checksum1 = NULL;
	g_autofree gchar *checksum2 = NULL;
	g_autofree gchar *filename = NULL;
	g_autoptr(FuArchive) archive = NULL;
	g_autoptr(GBytes) data = NULL;
	g_autoptr(GBytes) data_tmp1 = NULL;
	g_autoptr(GBytes) data_tmp2 = NULL;
	g_autoptr(GBytes) data_tmp3 = NULL;
	g_autoptr(GError) error = NULL;

#ifndef HAVE_LIBARCHIVE
	g_test_skip("no libarchive support");
	return;
#endif

	filename = g_test_build_filename(G_TEST_BUILT,
					 "tests",
					 "colorhug",
					 "colorhug-als-3.0.2.cab",
					 NULL);
	data = fu_bytes_get_contents(filename, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data);

	archive = fu_archive_new(data, FU_FIRMWARE_PARSE_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(archive);

	data_tmp1 = fu_archive_lookup_by_fn(archive, "firmware.metainfo.xml", &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_tmp1);
	checksum1 = g_compute_checksum_for_bytes(G_CHECKSUM_SHA1, data_tmp1);
	g_assert_cmpstr(checksum1, ==, "f62ee340c27bbb80229c3dd3cb2e78bddfc82d4f");

	data_tmp2 = fu_archive_lookup_by_fn(archive, "firmware.txt", &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_tmp2);
	checksum2 = g_compute_checksum_for_bytes(G_CHECKSUM_SHA1, data_tmp2);
	g_assert_cmpstr(checksum2, ==, "22596363b3de40b06f981fb85d82312e8c0ed511");

	data_tmp3 = fu_archive_lookup_by_fn(archive, "NOTGOINGTOEXIST.xml", &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_null(data_tmp3);
}

static void
fu_string_append_func(void)
{
	g_autoptr(GString) str = g_string_new(NULL);
	fwupd_codec_string_append(str, 0, "hdr", "");
	fwupd_codec_string_append(str, 0, "key", "value");
	fwupd_codec_string_append(str, 0, "key1", "value1");
	fwupd_codec_string_append(str, 1, "key2", "value2");
	fwupd_codec_string_append(str, 1, "", "value2");
	fwupd_codec_string_append(str, 2, "key3", "value3");
	g_assert_cmpstr(str->str,
			==,
			"hdr:\n"
			"key:                    value\n"
			"key1:                   value1\n"
			"  key2:                 value2\n"
			"                        value2\n"
			"    key3:               value3\n");
}

static void
fu_device_version_format_func(void)
{
	g_autoptr(FuDevice) device = fu_device_new(NULL);
	fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_ENSURE_SEMVER);
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device, "Ver1.2.3 RELEASE");
	g_assert_cmpstr(fu_device_get_version(device), ==, "1.2.3");
}

static void
fu_device_version_format_raw_func(void)
{
	g_autoptr(FuDevice) device = g_object_new(FU_TYPE_USB_DEVICE, NULL);

	/* like normal */
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_BCD);
	fu_device_set_version_raw(device, 256);
	fu_device_set_version_lowest_raw(device, 257);

	g_assert_cmpstr(fu_device_get_version(device), ==, "1.0");
	g_assert_cmpstr(fu_device_get_version_lowest(device), ==, "1.1");

	/* ensure both are changed */
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_PLAIN);
	g_assert_cmpstr(fu_device_get_version(device), ==, "256");
	g_assert_cmpstr(fu_device_get_version_lowest(device), ==, "257");
}

static void
fu_device_open_refcount_func(void)
{
	gboolean ret;
	g_autoptr(FuDevice) device = fu_device_new(NULL);
	g_autoptr(GError) error = NULL;
	fu_device_set_id(device, "test_device");
	ret = fu_device_open(device, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_device_open(device, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_device_close(device, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_device_close(device, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_device_close(device, &error);
	g_assert_true(ret);
}

static void
fu_device_rescan_func(void)
{
	gboolean ret;
	g_autoptr(FuDevice) device = fu_device_new(NULL);
	g_autoptr(GError) error = NULL;

	/* no GUIDs! */
	ret = fu_device_rescan(device, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_device_name_func(void)
{
	g_autoptr(FuDevice) device1 = fu_device_new(NULL);
	g_autoptr(FuDevice) device2 = fu_device_new(NULL);

	/* vendor then name */
	fu_device_set_vendor(device1, "  Hughski  ");
	fu_device_set_name(device1, "HUGHSKI  ColorHug(TM)__Pro  ");
	g_assert_cmpstr(fu_device_get_vendor(device1), ==, "Hughski");
	g_assert_cmpstr(fu_device_get_name(device1), ==, "ColorHug™ Pro");

	/* name then vendor */
	fu_device_set_name(device2, "Hughski ColorHug(TM)_Pro");
	fu_device_set_vendor(device2, "Hughski");
	g_assert_cmpstr(fu_device_get_vendor(device2), ==, "Hughski");
	g_assert_cmpstr(fu_device_get_name(device2), ==, "ColorHug™ Pro");

	/* a real example */
	fu_device_set_name(device2, "Intel(R) Core(TM) i7-10850H CPU @ 2.70GHz");
	fu_device_set_vendor(device2, "Intel");
	g_assert_cmpstr(fu_device_get_name(device2), ==, "Core™ i7-10850H CPU @ 2.70GHz");

	/* name and vendor are the same */
#ifndef SUPPORTED_BUILD
	g_test_expect_message("FuDevice", G_LOG_LEVEL_WARNING, "name and vendor are the same*");
#endif
	fu_device_set_name(device2, "example");
	fu_device_set_vendor(device2, "EXAMPLE");
	g_assert_cmpstr(fu_device_get_name(device2), ==, "example");
	g_assert_cmpstr(fu_device_get_vendor(device2), ==, "EXAMPLE");
}

static void
fu_device_cfi_device_func(void)
{
	gboolean ret;
	guint8 cmd = 0;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuCfiDevice) cfi_device = NULL;
	g_autoptr(GError) error = NULL;

	ret = fu_context_load_quirks(ctx, FU_QUIRKS_LOAD_FLAG_NO_CACHE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	cfi_device = fu_cfi_device_new(ctx, "3730");
	ret = fu_device_setup(FU_DEVICE(cfi_device), &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* fallback */
	ret = fu_cfi_device_get_cmd(cfi_device, FU_CFI_DEVICE_CMD_READ_DATA, &cmd, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(cmd, ==, 0x03);

	/* from quirk */
	ret = fu_cfi_device_get_cmd(cfi_device, FU_CFI_DEVICE_CMD_CHIP_ERASE, &cmd, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(cmd, ==, 0xC7);
	g_assert_cmpint(fu_cfi_device_get_size(cfi_device), ==, 0x10000);
	g_assert_cmpint(fu_cfi_device_get_page_size(cfi_device), ==, 0x200);
	g_assert_cmpint(fu_cfi_device_get_sector_size(cfi_device), ==, 0x2000);
	g_assert_cmpint(fu_cfi_device_get_block_size(cfi_device), ==, 0x8000);
}

static void
fu_device_metadata_func(void)
{
	g_autoptr(FuDevice) device = fu_device_new(NULL);

	/* string */
	fu_device_set_metadata(device, "foo", "bar");
	g_assert_cmpstr(fu_device_get_metadata(device, "foo"), ==, "bar");
	fu_device_set_metadata(device, "foo", "baz");
	g_assert_cmpstr(fu_device_get_metadata(device, "foo"), ==, "baz");
	g_assert_null(fu_device_get_metadata(device, "unknown"));

	/* boolean */
	fu_device_set_metadata_boolean(device, "baz", TRUE);
	g_assert_cmpstr(fu_device_get_metadata(device, "baz"), ==, "true");
	g_assert_true(fu_device_get_metadata_boolean(device, "baz"));
	g_assert_false(fu_device_get_metadata_boolean(device, "unknown"));

	/* integer */
	fu_device_set_metadata_integer(device, "bam", 12345);
	g_assert_cmpstr(fu_device_get_metadata(device, "bam"), ==, "12345");
	g_assert_cmpint(fu_device_get_metadata_integer(device, "bam"), ==, 12345);
	g_assert_cmpint(fu_device_get_metadata_integer(device, "unknown"), ==, G_MAXUINT);
}

static void
fu_smbios_func(void)
{
	const gchar *str;
	gboolean ret;
	g_autofree gchar *dump = NULL;
	g_autofree gchar *testdatadir = NULL;
	g_autofree gchar *full_path = NULL;
	g_autoptr(FuSmbios) smbios = NULL;
	g_autoptr(GError) error = NULL;

#ifdef _WIN32
	g_test_skip("Windows uses GetSystemFirmwareTable rather than parsing the fake test data");
	return;
#endif

	/* these tests will not write */
	testdatadir = g_test_build_filename(G_TEST_DIST, "tests", NULL);
	(void)g_setenv("FWUPD_SYSFSFWDIR", testdatadir, TRUE);

	full_path = g_test_build_filename(G_TEST_DIST, "tests", "dmi", "tables", NULL);
	if (!g_file_test(full_path, G_FILE_TEST_IS_DIR)) {
		g_test_skip("no DMI tables found");
		return;
	}

	smbios = fu_smbios_new();
	ret = fu_smbios_setup(smbios, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	dump = fu_firmware_to_string(FU_FIRMWARE(smbios));
	g_debug("%s", dump);

	/* test for missing table */
	str = fu_smbios_get_string(smbios, 0xff, FU_SMBIOS_STRUCTURE_LENGTH_ANY, 0, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_assert_null(str);
	g_clear_error(&error);

	/* check for invalid offset */
	str = fu_smbios_get_string(smbios,
				   FU_SMBIOS_STRUCTURE_TYPE_BIOS,
				   FU_SMBIOS_STRUCTURE_LENGTH_ANY,
				   0xff,
				   &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_assert_null(str);
	g_clear_error(&error);

	/* check for invalid length */
	str = fu_smbios_get_string(smbios, FU_SMBIOS_STRUCTURE_TYPE_BIOS, 0x01, 0xff, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_assert_null(str);
	g_clear_error(&error);

	/* get vendor -- explicit length */
	str = fu_smbios_get_string(smbios, FU_SMBIOS_STRUCTURE_TYPE_BIOS, 0x18, 0x04, &error);
	g_assert_no_error(error);
	g_assert_cmpstr(str, ==, "LENOVO");

	/* get vendor */
	str = fu_smbios_get_string(smbios,
				   FU_SMBIOS_STRUCTURE_TYPE_BIOS,
				   FU_SMBIOS_STRUCTURE_LENGTH_ANY,
				   0x04,
				   &error);
	g_assert_no_error(error);
	g_assert_cmpstr(str, ==, "LENOVO");
}

static void
fu_smbios3_func(void)
{
	const gchar *str;
	gboolean ret;
	g_autofree gchar *dump = NULL;
	g_autofree gchar *path = NULL;
	g_autoptr(FuSmbios) smbios = NULL;
	g_autoptr(GError) error = NULL;

	path = g_test_build_filename(G_TEST_DIST, "tests", "dmi", "tables64", NULL);

	if (!g_file_test(path, G_FILE_TEST_IS_DIR)) {
		g_test_skip("no DMI tables found");
		return;
	}

	smbios = fu_smbios_new();
	ret = fu_smbios_setup_from_path(smbios, path, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	dump = fu_firmware_to_string(FU_FIRMWARE(smbios));
	g_debug("%s", dump);

	/* get vendor */
	str = fu_smbios_get_string(smbios, FU_SMBIOS_STRUCTURE_TYPE_BIOS, 0x18, 0x04, &error);
	g_assert_no_error(error);
	g_assert_cmpstr(str, ==, "Dell Inc.");
}

static void
fu_context_efivars_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(GError) error = NULL;

	ret = fu_context_efivars_check_free_space(ctx, 10240, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	ret = fu_context_efivars_check_free_space(ctx, 10241, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_BROKEN_SYSTEM);
	g_assert_false(ret);
}

static void
fu_context_backends_func(void)
{
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuBackend) backend2 = NULL;
	g_autoptr(FuBackend) backend = g_object_new(FU_TYPE_BACKEND, "name", "dummy", NULL);
	g_autoptr(GError) error = NULL;

	fu_context_add_backend(ctx, backend);
	backend2 = fu_context_get_backend_by_name(ctx, "dummy", &error);
	g_assert_no_error(error);
	g_assert_nonnull(backend2);
}

static void
fu_context_flags_func(void)
{
	g_autoptr(FuContext) ctx = fu_context_new();

	g_assert_false(fu_context_has_flag(ctx, FU_CONTEXT_FLAG_SAVE_EVENTS));
	fu_context_add_flag(ctx, FU_CONTEXT_FLAG_SAVE_EVENTS);
	g_assert_true(fu_context_has_flag(ctx, FU_CONTEXT_FLAG_SAVE_EVENTS));
	fu_context_remove_flag(ctx, FU_CONTEXT_FLAG_SAVE_EVENTS);
	fu_context_remove_flag(ctx, FU_CONTEXT_FLAG_SAVE_EVENTS);
	g_assert_false(fu_context_has_flag(ctx, FU_CONTEXT_FLAG_SAVE_EVENTS));
	fu_context_add_flag(ctx, FU_CONTEXT_FLAG_SAVE_EVENTS);
	fu_context_add_flag(ctx, FU_CONTEXT_FLAG_SAVE_EVENTS);
	g_assert_true(fu_context_has_flag(ctx, FU_CONTEXT_FLAG_SAVE_EVENTS));
}

static void
fu_context_udev_subsystems_func(void)
{
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) plugin_names1 = NULL;
	g_autoptr(GPtrArray) plugin_names2 = NULL;
	g_autoptr(GPtrArray) udev_subsystems = NULL;

	/* ensure we add the base subsystem too */
	fu_context_add_udev_subsystem(ctx, "usb", NULL);
	fu_context_add_udev_subsystem(ctx, "block:partition", "uf2");
	udev_subsystems = fu_context_get_udev_subsystems(ctx);
	g_assert_nonnull(udev_subsystems);
	g_assert_cmpint(udev_subsystems->len, ==, 3);

	/* add another plugin that can handle *all* block devices */
	fu_context_add_udev_subsystem(ctx, "block", "uf3");

	/* both specified, so return uf2 and uf3 */
	plugin_names1 =
	    fu_context_get_plugin_names_for_udev_subsystem(ctx, "block:partition", &error);
	g_assert_no_error(error);
	g_assert_nonnull(plugin_names1);
	g_assert_cmpint(plugin_names1->len, ==, 2);

	/* devtype unset, so just uf3 */
	plugin_names2 = fu_context_get_plugin_names_for_udev_subsystem(ctx, "block", &error);
	g_assert_no_error(error);
	g_assert_nonnull(plugin_names2);
	g_assert_cmpint(plugin_names2->len, ==, 1);
}

static void
fu_context_state_func(void)
{
	g_autoptr(FuContext) ctx = fu_context_new();

	g_assert_cmpint(fu_context_get_power_state(ctx), ==, FU_POWER_STATE_UNKNOWN);
	g_assert_cmpint(fu_context_get_lid_state(ctx), ==, FU_LID_STATE_UNKNOWN);
	g_assert_cmpint(fu_context_get_display_state(ctx), ==, FU_DISPLAY_STATE_UNKNOWN);
	g_assert_cmpint(fu_context_get_battery_level(ctx), ==, FWUPD_BATTERY_LEVEL_INVALID);

	fu_context_set_power_state(ctx, FU_POWER_STATE_BATTERY);
	fu_context_set_power_state(ctx, FU_POWER_STATE_BATTERY);
	fu_context_set_lid_state(ctx, FU_LID_STATE_CLOSED);
	fu_context_set_lid_state(ctx, FU_LID_STATE_CLOSED);
	fu_context_set_display_state(ctx, FU_DISPLAY_STATE_CONNECTED);
	fu_context_set_display_state(ctx, FU_DISPLAY_STATE_CONNECTED);
	fu_context_set_battery_level(ctx, 50);
	fu_context_set_battery_level(ctx, 50);

	g_assert_cmpint(fu_context_get_power_state(ctx), ==, FU_POWER_STATE_BATTERY);
	g_assert_cmpint(fu_context_get_lid_state(ctx), ==, FU_LID_STATE_CLOSED);
	g_assert_cmpint(fu_context_get_display_state(ctx), ==, FU_DISPLAY_STATE_CONNECTED);
	g_assert_cmpint(fu_context_get_battery_level(ctx), ==, 50);
}

static void
fu_context_firmware_gtypes_func(void)
{
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(GArray) gtypes = NULL;
	g_autoptr(GPtrArray) gtype_ids = NULL;

	fu_context_add_firmware_gtype(ctx, FU_TYPE_FIRMWARE);

	gtype_ids = fu_context_get_firmware_gtype_ids(ctx);
	g_assert_nonnull(gtype_ids);
	g_assert_cmpint(gtype_ids->len, ==, 1);
	g_assert_cmpstr(g_ptr_array_index(gtype_ids, 0), ==, "raw");

	gtypes = fu_context_get_firmware_gtypes(ctx);
	g_assert_nonnull(gtypes);
	g_assert_cmpint(gtypes->len, ==, 1);
	g_assert_cmpint(g_array_index(gtypes, GType, 0), ==, FU_TYPE_FIRMWARE);

	g_assert_cmpint(fu_context_get_firmware_gtype_by_id(ctx, "raw"), ==, FU_TYPE_FIRMWARE);
	g_assert_cmpint(fu_context_get_firmware_gtype_by_id(ctx, "n/a"), ==, G_TYPE_INVALID);
}

static void
fu_context_hwids_dmi_func(void)
{
	g_autofree gchar *dump = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;
	gboolean ret;

	ret = fu_context_load_hwinfo(ctx, progress, FU_CONTEXT_HWID_FLAG_LOAD_DMI, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	dump = fu_firmware_to_string(FU_FIRMWARE(fu_context_get_smbios(ctx)));
	g_debug("%s", dump);

	g_assert_cmpstr(fu_context_get_hwid_value(ctx, FU_HWIDS_KEY_MANUFACTURER), ==, "FwupdTest");
	g_assert_cmpuint(fu_context_get_chassis_kind(ctx), ==, 16);
}

static void
fu_context_hwids_unset_func(void)
{
	g_autofree gchar *testdatadir = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;
	gboolean ret;

	testdatadir = g_test_build_filename(G_TEST_DIST, "tests", NULL);
	(void)g_setenv("FWUPD_SYSCONFDIR", testdatadir, TRUE);

	ret = fu_context_load_hwinfo(ctx, progress, FU_CONTEXT_HWID_FLAG_LOAD_CONFIG, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_context_load_quirks(ctx, FU_QUIRKS_LOAD_FLAG_NO_CACHE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* ensure that we processed the ~hwid-test-flag */
	g_assert_false(fu_context_has_hwid_flag(ctx, "hwid-test-flag"));
}

static void
fu_context_hwids_fdt_func(void)
{
	gboolean ret;
	g_autofree gchar *dump = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuFirmware) fdt_tmp = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GFile) file =
	    g_file_new_for_path("/tmp/fwupd-self-test/var/lib/fwupd/system.dtb");

	/* write file */
	fdt_tmp = fu_firmware_new_from_xml(
	    "<firmware gtype=\"FuFdtFirmware\">\n"
	    "  <firmware gtype=\"FuFdtImage\">\n"
	    "    <metadata key=\"compatible\" format=\"str\">pine64,rockpro64-v2.1</metadata>\n"
	    "    <metadata key=\"chassis-type\" format=\"str\">tablet</metadata>\n"
	    "    <metadata key=\"vendor\" format=\"str\">fwupd</metadata>\n"
	    "    <firmware gtype=\"FuFdtImage\">\n"
	    "      <id>ibm,firmware-versions</id>\n"
	    "      <metadata key=\"version\" format=\"str\">1.2.3</metadata>\n"
	    "    </firmware>\n"
	    "    <firmware gtype=\"FuFdtImage\">\n"
	    "      <id>vpd</id>\n"
	    "      <firmware gtype=\"FuFdtImage\">\n"
	    "        <id>root-node-vpd@a000</id>\n"
	    "        <firmware gtype=\"FuFdtImage\">\n"
	    "          <id>enclosure@1e00</id>\n"
	    "          <firmware gtype=\"FuFdtImage\">\n"
	    "            <id>backplane@800</id>\n"
	    "            <metadata key=\"part-number\" format=\"str\">Tablet</metadata>\n"
	    "          </firmware>\n"
	    "        </firmware>\n"
	    "      </firmware>\n"
	    "    </firmware>\n"
	    "  </firmware>\n"
	    "</firmware>\n",
	    &error);
	g_assert_no_error(error);
	g_assert_nonnull(fdt_tmp);
	ret = fu_firmware_write_file(FU_FIRMWARE(fdt_tmp), file, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	ret = fu_context_load_hwinfo(ctx, progress, FU_CONTEXT_HWID_FLAG_LOAD_FDT, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	dump = fu_firmware_to_string(FU_FIRMWARE(fu_context_get_smbios(ctx)));
	g_debug("%s", dump);

	g_assert_cmpstr(fu_context_get_hwid_value(ctx, FU_HWIDS_KEY_MANUFACTURER), ==, "fwupd");
	g_assert_cmpstr(fu_context_get_hwid_value(ctx, FU_HWIDS_KEY_BASEBOARD_PRODUCT),
			==,
			"Tablet");
	g_assert_cmpuint(fu_context_get_chassis_kind(ctx), ==, FU_SMBIOS_CHASSIS_KIND_TABLET);
}

static void
fu_common_olson_timezone_id_func(void)
{
	g_autofree gchar *timezone_id = NULL;
	g_autoptr(GError) error = NULL;

#ifdef HOST_MACHINE_SYSTEM_DARWIN
	g_test_skip("not supported on Darwin");
	return;
#endif

	timezone_id = fu_common_get_olson_timezone_id(&error);
	g_assert_no_error(error);
#ifdef _WIN32
	/* we do not emulate this on Windows, so just check for anything */
	g_assert_nonnull(timezone_id);
#else
	g_assert_cmpstr(timezone_id, ==, "America/New_York");
#endif
}

static void
fu_cpuid_func(void)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GHashTable) cpu_attrs = NULL;

	cpu_attrs = fu_cpu_get_attrs(&error);
	g_assert_no_error(error);
	g_assert_nonnull(cpu_attrs);
	g_assert_cmpstr(g_hash_table_lookup(cpu_attrs, "vendor_id"), ==, "AuthenticAMD");
	g_assert_cmpstr(g_hash_table_lookup(cpu_attrs, "fpu_exception"), ==, "yes");
}

static void
fu_strsafe_func(void)
{
	struct {
		const gchar *in;
		const gchar *op;
	} strs[] = {{"dave123", "dave123"},
		    {"dave123XXX", "dave123"},
		    {"dave\x03XXX", "dave.XX"},
		    {"dave\x03\x04XXX", "dave..X"},
		    {"\x03\x03", NULL},
		    {NULL, NULL}};
	GPtrArray *instance_ids;
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) dev = fu_device_new(ctx);
	g_autoptr(GError) error = NULL;

	/* check bespoke legacy instance ID behavior */
	fu_device_add_instance_strsafe(dev, "KEY", "_ _LEN&VO&\\&");
	ret = fu_device_build_instance_id(dev, &error, "SUB", "KEY", NULL);
	g_assert_no_error(error);
	g_assert_true(ret);
	fu_device_convert_instance_ids(dev);
	instance_ids = fu_device_get_instance_ids(dev);
	g_assert_cmpint(instance_ids->len, ==, 1);
	g_assert_cmpstr(g_ptr_array_index(instance_ids, 0), ==, "SUB\\KEY_LEN-VO");

	for (guint i = 0; strs[i].in != NULL; i++) {
		g_autofree gchar *tmp = fu_strsafe(strs[i].in, 7);
		g_assert_cmpstr(tmp, ==, strs[i].op);
	}
}

static void
fu_hwids_func(void)
{
	g_autofree gchar *full_path = NULL;
	g_autoptr(FuContext) context = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;
	gboolean ret;

	struct {
		const gchar *key;
		const gchar *value;
	} guids[] = {{"Manufacturer", "6de5d951-d755-576b-bd09-c5cf66b27234"},
		     {"HardwareID-14", "6de5d951-d755-576b-bd09-c5cf66b27234"},
		     {"HardwareID-13", "f8e1de5f-b68c-5f52-9d1a-f1ba52f1f773"},
		     {"HardwareID-12", "e093d715-70f7-51f4-b6c8-b4a7e31def85"},
		     {"HardwareID-11", "db73af4c-4612-50f7-b8a7-787cf4871847"},
		     {"HardwareID-10", "f4275c1f-6130-5191-845c-3426247eb6a1"},
		     {"HardwareID-09", "0cf8618d-9eff-537c-9f35-46861406eb9c"},
		     {"HardwareID-08", "059eb22d-6dc7-59af-abd3-94bbe017f67c"},
		     {"HardwareID-07", "da1da9b6-62f5-5f22-8aaa-14db7eeda2a4"},
		     {"HardwareID-06", "178cd22d-ad9f-562d-ae0a-34009822cdbe"},
		     {"HardwareID-05", "8dc9b7c5-f5d5-5850-9ab3-bd6f0549d814"},
		     {"HardwareID-04", "660ccba8-1b78-5a33-80e6-9fb8354ee873"},
		     {"HardwareID-03", "3faec92a-3ae3-5744-be88-495e90a7d541"},
		     {"HardwareID-02", "f5ff077f-3eeb-5bae-be1c-e98ffe8ce5f8"},
		     {"HardwareID-01", "b7cceb67-774c-537e-bf8b-22c6107e9a74"},
		     {"HardwareID-00", "147efce9-f201-5fc8-ab0c-c859751c3440"},
		     {NULL, NULL}};

#ifdef _WIN32
	g_test_skip("Windows uses GetSystemFirmwareTable rather than parsing the fake test data");
	return;
#endif

	/* DMI */
	full_path = g_test_build_filename(G_TEST_DIST, "tests", "dmi", "tables", NULL);
	if (!g_file_test(full_path, G_FILE_TEST_IS_DIR)) {
		g_test_skip("no DMI tables found");
		return;
	}

	ret = fu_context_load_hwinfo(context, progress, FU_CONTEXT_HWID_FLAG_LOAD_SMBIOS, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	g_assert_cmpstr(fu_context_get_hwid_value(context, FU_HWIDS_KEY_MANUFACTURER),
			==,
			"LENOVO");
	g_assert_cmpstr(fu_context_get_hwid_value(context, FU_HWIDS_KEY_ENCLOSURE_KIND), ==, "a");
	g_assert_cmpstr(fu_context_get_hwid_value(context, FU_HWIDS_KEY_FAMILY),
			==,
			"ThinkPad T440s");
	g_assert_cmpstr(fu_context_get_hwid_value(context, FU_HWIDS_KEY_PRODUCT_NAME),
			==,
			"20ARS19C0C");
	g_assert_cmpstr(fu_context_get_hwid_value(context, FU_HWIDS_KEY_BIOS_VENDOR), ==, "LENOVO");
	g_assert_cmpstr(fu_context_get_hwid_value(context, FU_HWIDS_KEY_BIOS_VERSION),
			==,
			"GJET75WW (2.25 )");
	g_assert_cmpstr(fu_context_get_hwid_value(context, FU_HWIDS_KEY_BIOS_MAJOR_RELEASE),
			==,
			"02");
	g_assert_cmpstr(fu_context_get_hwid_value(context, FU_HWIDS_KEY_BIOS_MINOR_RELEASE),
			==,
			"19");
	g_assert_cmpstr(fu_context_get_hwid_value(context, FU_HWIDS_KEY_PRODUCT_SKU),
			==,
			"LENOVO_MT_20AR_BU_Think_FM_ThinkPad T440s");
	for (guint i = 0; guids[i].key != NULL; i++) {
		FuHwids *hwids = fu_context_get_hwids(context);
		g_autofree gchar *guid = fu_hwids_get_guid(hwids, guids[i].key, &error);
		g_assert_no_error(error);
		g_assert_cmpstr(guid, ==, guids[i].value);
	}
	for (guint i = 0; guids[i].key != NULL; i++)
		g_assert_true(fu_context_has_hwid_guid(context, guids[i].value));
}

static void
fu_test_plugin_device_added_cb(FuPlugin *plugin, FuDevice *device, gpointer user_data)
{
	FuDevice **dev = (FuDevice **)user_data;
	*dev = g_object_ref(device);
	fu_test_loop_quit();
}

static void
fu_config_func(void)
{
	GStatBuf statbuf = {0};
	gboolean ret;
	g_autofree gchar *composite_data = NULL;
	g_autoptr(FuConfig) config = fu_config_new();
	g_autoptr(GError) error = NULL;
	g_autofree gchar *fn_imu = NULL;
	g_autofree gchar *fn_mut = NULL;

#ifdef _WIN32
	/* the Windows file permission model is different than a simple octal value */
	g_test_skip("chmod not supported on Windows");
	return;
#endif

	/* immutable file */
	(void)g_setenv("FWUPD_SYSCONFDIR", "/tmp/fwupd-self-test/etc/fwupd", TRUE);
	fn_imu = g_build_filename(g_getenv("FWUPD_SYSCONFDIR"), "fwupd.conf", NULL);
	g_assert_nonnull(fn_imu);
	ret = fu_path_mkdir_parent(fn_imu, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_remove(fn_imu);
	ret = g_file_set_contents(fn_imu,
				  "[fwupd]\n"
				  "Key=true\n",
				  -1,
				  &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	g_chmod(fn_imu, 0640);
	ret = g_stat(fn_imu, &statbuf);
	g_assert_cmpint(ret, ==, 0);
	g_assert_cmpint(statbuf.st_mode & 0777, ==, 0640);

	/* mutable file */
	(void)g_setenv("LOCALCONF_DIRECTORY", "/tmp/fwupd-self-test/var/etc/fwupd", TRUE);
	fn_mut = g_build_filename(g_getenv("LOCALCONF_DIRECTORY"), "fwupd.conf", NULL);
	g_assert_nonnull(fn_mut);
	ret = fu_path_mkdir_parent(fn_mut, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_remove(fn_mut);
	ret = g_file_set_contents(fn_mut,
				  "# group comment\n"
				  "[fwupd]\n"
				  "# key comment\n"
				  "Key=false\n",
				  -1,
				  &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	ret = fu_config_load(config, FU_CONFIG_LOAD_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	ret = fu_config_set_value(config, "fwupd", "Key", "false", &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	ret = g_file_get_contents(fn_mut, &composite_data, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_true(g_strstr_len(composite_data, -1, "Key=false") != NULL);
	g_assert_true(g_strstr_len(composite_data, -1, "Key=true") == NULL);
	g_assert_true(g_strstr_len(composite_data, -1, "# group comment") != NULL);
	g_assert_true(g_strstr_len(composite_data, -1, "# key comment") != NULL);
	g_remove(fn_mut);
}

static void
fu_plugin_config_func(void)
{
	GStatBuf statbuf = {0};
	gboolean ret;
	gint rc;
	g_autofree gchar *fn = NULL;
	g_autofree gchar *value = NULL;
	g_autofree gchar *value_missing = NULL;
	g_autofree gchar *fn_mut = NULL;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuPlugin) plugin = fu_plugin_new(ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;

#ifdef _WIN32
	/* the Windows file permission model is different than a simple octal value */
	g_test_skip("chmod not supported on Windows");
	return;
#endif

	/* remove existing file */
	(void)g_setenv("FWUPD_SYSCONFDIR", "/tmp/fwupd-self-test/etc/fwupd", TRUE);
	fu_plugin_set_name(plugin, "test");
	fn = fu_path_build(FU_PATH_KIND_SYSCONFDIR_PKG, "fwupd.conf", NULL);
	ret = fu_path_mkdir_parent(fn, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_remove(fn);
	ret = g_file_set_contents(fn, "", -1, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* mutable file we'll be writing */
	(void)g_setenv("LOCALCONF_DIRECTORY", "/tmp/fwupd-self-test/var/etc/fwupd", TRUE);
	fn_mut = g_build_filename(g_getenv("LOCALCONF_DIRECTORY"), "fwupd.conf", NULL);
	g_assert_nonnull(fn_mut);
	ret = fu_path_mkdir_parent(fn_mut, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_remove(fn_mut);
	ret = g_file_set_contents(fn_mut, "", -1, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* load context */
	ret = fu_context_load_hwinfo(ctx, progress, FU_CONTEXT_HWID_FLAG_FIX_PERMISSIONS, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* set a value */
	ret = fu_plugin_set_config_value(plugin, "Key", "True", &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_true(g_file_test(fn, G_FILE_TEST_EXISTS));

	/* check it is only readable by the user/group */
	rc = g_stat(fn_mut, &statbuf);
	g_assert_cmpint(rc, ==, 0);
	g_assert_cmpint(statbuf.st_mode & 0777, ==, 0640);

	/* read back the value */
	fu_plugin_set_config_default(plugin, "NotGoingToExist", "Foo");
	value_missing = fu_plugin_get_config_value(plugin, "NotGoingToExist");
	g_assert_cmpstr(value_missing, ==, "Foo");
	value = fu_plugin_get_config_value(plugin, "Key");
	g_assert_cmpstr(value, ==, "True");
	g_assert_true(fu_plugin_get_config_value_boolean(plugin, "Key"));
}

static void
fu_plugin_devices_func(void)
{
	g_autoptr(FuDevice) device = fu_device_new(NULL);
	g_autoptr(FuDevice) child = fu_device_new(NULL);
	g_autoptr(FuPlugin) plugin = fu_plugin_new(NULL);
	GPtrArray *devices;

	devices = fu_plugin_get_devices(plugin);
	g_assert_nonnull(devices);
	g_assert_cmpint(devices->len, ==, 0);

	fu_device_set_id(device, "testdev");
	fu_device_set_name(device, "testdev");
	fu_plugin_device_add(plugin, device);
	g_assert_cmpint(devices->len, ==, 1);
	fu_plugin_device_remove(plugin, device);
	g_assert_cmpint(devices->len, ==, 0);

	/* add a child after adding the parent to the plugin */
	fu_device_set_id(child, "child");
	fu_device_set_name(child, "child");
	fu_device_add_child(device, child);
	g_assert_cmpint(devices->len, ==, 1);

	/* remove said child */
	fu_device_remove_child(device, child);
	g_assert_cmpint(devices->len, ==, 0);
}

static void
fu_plugin_device_inhibit_children_func(void)
{
	g_autoptr(FuDevice) parent = fu_device_new(NULL);
	g_autoptr(FuDevice) child1 = fu_device_new(NULL);
	g_autoptr(FuDevice) child2 = fu_device_new(NULL);

	fu_device_set_id(parent, "testdev");
	fu_device_set_name(parent, "testdev");
	fu_device_add_flag(parent, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_set_id(child1, "child1");
	fu_device_set_name(child1, "child1");
	fu_device_add_flag(child1, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_child(parent, child1);

	/* inhibit the parent */
	fu_device_inhibit(parent, "test", "because");
	g_assert_false(fu_device_has_flag(parent, FWUPD_DEVICE_FLAG_UPDATABLE));
	g_assert_true(fu_device_has_flag(child1, FWUPD_DEVICE_FLAG_UPDATABLE));
	fu_device_uninhibit(parent, "test");

	/* make the inhibit propagate to children */
	fu_device_add_private_flag(parent, FU_DEVICE_PRIVATE_FLAG_INHIBIT_CHILDREN);
	fu_device_inhibit(parent, "test", "because");
	g_assert_false(fu_device_has_flag(parent, FWUPD_DEVICE_FLAG_UPDATABLE));
	g_assert_false(fu_device_has_flag(child1, FWUPD_DEVICE_FLAG_UPDATABLE));

	/* add a child after the inhibit, which should also be inhibited too */
	fu_device_set_id(child2, "child2");
	fu_device_set_name(child2, "child2");
	fu_device_add_flag(child2, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_child(parent, child2);
	g_assert_false(fu_device_has_flag(parent, FWUPD_DEVICE_FLAG_UPDATABLE));
	g_assert_false(fu_device_has_flag(child1, FWUPD_DEVICE_FLAG_UPDATABLE));
	g_assert_false(fu_device_has_flag(child2, FWUPD_DEVICE_FLAG_UPDATABLE));
}

static void
fu_plugin_delay_func(void)
{
	FuDevice *device_tmp;
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuDevice) device = NULL;

	plugin = fu_plugin_new(NULL);
	g_signal_connect(FU_PLUGIN(plugin),
			 "device-added",
			 G_CALLBACK(fu_test_plugin_device_added_cb),
			 &device_tmp);
	g_signal_connect(FU_PLUGIN(plugin),
			 "device-removed",
			 G_CALLBACK(fu_test_plugin_device_added_cb),
			 &device_tmp);

	/* add device straight away */
	device = fu_device_new(NULL);
	fu_device_set_id(device, "testdev");
	fu_plugin_device_add(plugin, device);
	g_assert_nonnull(device_tmp);
	g_assert_cmpstr(fu_device_get_id(device_tmp),
			==,
			"b7eccd0059d6d7dc2ef76c35d6de0048cc8c029d");
	g_clear_object(&device_tmp);

	/* remove device */
	fu_plugin_device_remove(plugin, device);
	g_assert_nonnull(device_tmp);
	g_assert_cmpstr(fu_device_get_id(device_tmp),
			==,
			"b7eccd0059d6d7dc2ef76c35d6de0048cc8c029d");
	g_clear_object(&device_tmp);
}

static void
fu_plugin_fdt_func(void)
{
	gboolean ret;
	g_autofree gchar *compatible = NULL;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuFirmware) fdt = NULL;
	g_autoptr(FuFirmware) fdt_root = NULL;
	g_autoptr(FuFirmware) fdt_tmp = NULL;
	g_autoptr(FuFirmware) img2 = NULL;
	g_autoptr(FuFirmware) img3 = NULL;
	g_autoptr(FuFirmware) img4 = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GFile) file =
	    g_file_new_for_path("/tmp/fwupd-self-test/var/lib/fwupd/system.dtb");

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

static void
fu_plugin_quirks_func(void)
{
	const gchar *tmp;
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(GError) error = NULL;

	ret = fu_context_load_quirks(ctx, FU_QUIRKS_LOAD_FLAG_NO_CACHE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* USB\\VID_0A5C&PID_6412 */
	tmp = fu_context_lookup_quirk_by_id(ctx, "7a1ba7b9-6bcd-54a4-8a36-d60cc5ee935c", "Flags");
	g_assert_cmpstr(tmp, ==, "ignore-runtime");

	/* ACME Inc.=True */
	tmp = fu_context_lookup_quirk_by_id(ctx, "ec77e295-7c63-5935-9957-be0472d9593a", "Name");
	g_assert_cmpstr(tmp, ==, "awesome");

	/* CORP* */
	tmp = fu_context_lookup_quirk_by_id(ctx, "3731cce4-484c-521f-a652-892c8e0a65c7", "Name");
	g_assert_cmpstr(tmp, ==, "town");

	/* baz */
	tmp = fu_context_lookup_quirk_by_id(ctx, "579a3b1c-d1db-5bdc-b6b9-e2c1b28d5b8a", "Unfound");
	g_assert_cmpstr(tmp, ==, NULL);

	/* unfound */
	tmp = fu_context_lookup_quirk_by_id(ctx, "8ff2ed23-b37e-5f61-b409-b7fe9563be36", "tests");
	g_assert_cmpstr(tmp, ==, NULL);

	/* unfound */
	tmp = fu_context_lookup_quirk_by_id(ctx, "8ff2ed23-b37e-5f61-b409-b7fe9563be36", "unfound");
	g_assert_cmpstr(tmp, ==, NULL);

	/* GUID */
	tmp = fu_context_lookup_quirk_by_id(ctx, "bb9ec3e2-77b3-53bc-a1f1-b05916715627", "Flags");
	g_assert_cmpstr(tmp, ==, "clever");
}

static void
fu_plugin_quirks_performance_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuQuirks) quirks = fu_quirks_new(ctx);
	g_autoptr(GTimer) timer = g_timer_new();
	g_autoptr(GError) error = NULL;
	const gchar *keys[] = {"Name", "Children", "Flags", NULL};

	ret = fu_quirks_load(quirks, FU_QUIRKS_LOAD_FLAG_NO_CACHE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* lookup */
	g_timer_reset(timer);
	for (guint j = 0; j < 1000; j++) {
		const gchar *group = "bb9ec3e2-77b3-53bc-a1f1-b05916715627";
		for (guint i = 0; keys[i] != NULL; i++) {
			const gchar *tmp = fu_quirks_lookup_by_id(quirks, group, keys[i]);
			g_assert_cmpstr(tmp, !=, NULL);
		}
	}
	g_print("lookup=%.3fms ", g_timer_elapsed(timer, NULL) * 1000.f);
}

typedef struct {
	gboolean seen_one;
	gboolean seen_two;
} FuPluginQuirksAppendHelper;

static void
fu_plugin_quirks_append_cb(FuQuirks *quirks,
			   const gchar *key,
			   const gchar *value,
			   FuContextQuirkSource source,
			   gpointer user_data)
{
	FuPluginQuirksAppendHelper *helper = (FuPluginQuirksAppendHelper *)user_data;
	g_debug("key=%s, value=%s", key, value);
	if (g_strcmp0(key, "Plugin") == 0 && g_strcmp0(value, "one") == 0) {
		helper->seen_one = TRUE;
		return;
	}
	if (g_strcmp0(key, "Plugin") == 0 && g_strcmp0(value, "two") == 0) {
		helper->seen_two = TRUE;
		return;
	}
	g_assert_not_reached();
}

static void
fu_plugin_device_progress_func(void)
{
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuDevice) device = fu_device_new(ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuDeviceProgress) device_progress = fu_device_progress_new(device, progress);

	/* proxy */
	fu_progress_set_percentage(progress, 50);
	fu_progress_set_status(progress, FWUPD_STATUS_SHUTDOWN);
	g_assert_cmpint(fu_device_get_percentage(device), ==, 50);
	g_assert_cmpint(fu_device_get_status(device), ==, FWUPD_STATUS_SHUTDOWN);

	/* clear */
	g_clear_object(&device_progress);
	g_assert_cmpint(fu_device_get_percentage(device), ==, 0);
	g_assert_cmpint(fu_device_get_status(device), ==, FWUPD_STATUS_IDLE);

	/* do not proxy */
	fu_progress_set_percentage(progress, 100);
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_BUSY);
	g_assert_cmpint(fu_device_get_percentage(device), ==, 0);
	g_assert_cmpint(fu_device_get_status(device), ==, FWUPD_STATUS_IDLE);
}

static void
fu_plugin_quirks_append_func(void)
{
	FuPluginQuirksAppendHelper helper = {0};
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuQuirks) quirks = fu_quirks_new(ctx);
	g_autoptr(GError) error = NULL;

	/* lookup a duplicate group name */
	ret = fu_quirks_load(quirks, FU_QUIRKS_LOAD_FLAG_NO_CACHE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_quirks_lookup_by_id_iter(quirks,
					  "b19d1c67-a29a-51ce-9cae-f7b40fe5505b",
					  NULL,
					  fu_plugin_quirks_append_cb,
					  &helper);
	g_assert_true(ret);
	g_assert_true(helper.seen_one);
	g_assert_true(helper.seen_two);
}

static void
fu_quirks_vendor_ids_func(void)
{
	gboolean ret;
	const gchar *tmp;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autofree gchar *guid1 = fwupd_guid_hash_string("PCI\\VEN_8086");
	g_autofree gchar *guid2 = fwupd_guid_hash_string("USB\\VID_8086");
	g_autofree gchar *guid3 = fwupd_guid_hash_string("PNP\\VID_ICO");
	g_autofree gchar *guid4 = fwupd_guid_hash_string("PCI\\VEN_8086&DEV_0007");
	g_autofree gchar *guid5 = fwupd_guid_hash_string("USB\\VID_8086&PID_0001");
	g_autofree gchar *quirksdb = fu_path_build(FU_PATH_KIND_CACHEDIR_PKG, "quirks.db", NULL);
	g_autoptr(FuQuirks) quirks = fu_quirks_new(ctx);
	g_autoptr(GError) error = NULL;

	g_debug("deleting %s if exists", quirksdb);
	g_unlink(quirksdb);

	/* lookup a duplicate group name */
	ret = fu_quirks_load(quirks, FU_QUIRKS_LOAD_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	tmp = fu_quirks_lookup_by_id(quirks, guid1, "Vendor");
	g_assert_true(ret);
	g_assert_cmpstr(tmp, ==, "Intel Corporation");
	tmp = fu_quirks_lookup_by_id(quirks, guid2, "Vendor");
	g_assert_true(ret);
	g_assert_cmpstr(tmp, ==, "Intel Corp.");
	tmp = fu_quirks_lookup_by_id(quirks, guid3, FWUPD_RESULT_KEY_VENDOR);
	g_assert_true(ret);
	g_assert_cmpstr(tmp, ==, "Intel Corp");
	tmp = fu_quirks_lookup_by_id(quirks, guid4, FWUPD_RESULT_KEY_NAME);
	g_assert_true(ret);
	g_assert_cmpstr(tmp, ==, "82379AB");
	tmp = fu_quirks_lookup_by_id(quirks, guid5, FWUPD_RESULT_KEY_NAME);
	g_assert_true(ret);
	g_assert_cmpstr(tmp, ==, "AnyPoint (TM) Home Network 1.6 Mbps Wireless Adapter");
}

static void
fu_plugin_func(void)
{
	GHashTable *metadata;
	GPtrArray *rules;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuPlugin) plugin = fu_plugin_new(ctx);

	fu_plugin_add_rule(plugin, FU_PLUGIN_RULE_CONFLICTS, "dave1");
	fu_plugin_add_rule(plugin, FU_PLUGIN_RULE_CONFLICTS, "dave2");
	rules = fu_plugin_get_rules(plugin, FU_PLUGIN_RULE_CONFLICTS);
	g_assert_nonnull(rules);
	g_assert_cmpint(rules->len, ==, 2);
	rules = fu_plugin_get_rules(plugin, FU_PLUGIN_RULE_RUN_AFTER);
	g_assert_null(rules);

	fu_plugin_add_report_metadata(plugin, "key", "value");
	metadata = fu_plugin_get_report_metadata(plugin);
	g_assert_nonnull(metadata);
	g_assert_cmpint(g_hash_table_size(metadata), ==, 1);
}

static void
fu_plugin_vfuncs_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuPlugin) plugin = fu_plugin_new(ctx);
	g_autoptr(GError) error = NULL;

	/* nop: error */
	ret = fu_plugin_runner_modify_config(plugin, "foo", "bar", &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_false(ret);
	g_clear_error(&error);
}

static void
fu_plugin_device_gtype_func(void)
{
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuPlugin) plugin = fu_plugin_new(ctx);

	/* add the same gtype multiple times */
	fu_plugin_add_device_gtype(plugin, FU_TYPE_DEVICE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_DEVICE);
	g_assert_cmpint(fu_plugin_get_device_gtype_default(plugin), ==, FU_TYPE_DEVICE);

	/* now there's no explicit default */
	fu_plugin_add_device_gtype(plugin, FU_TYPE_SELF_TEST_DEVICE);
	g_assert_cmpint(fu_plugin_get_device_gtype_default(plugin), ==, G_TYPE_INVALID);

	/* make it explicit */
	fu_plugin_set_device_gtype_default(plugin, FU_TYPE_SELF_TEST_DEVICE);
	g_assert_cmpint(fu_plugin_get_device_gtype_default(plugin), ==, FU_TYPE_SELF_TEST_DEVICE);
}

static void
fu_plugin_backend_device_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuDevice) device = fu_device_new(ctx);
	g_autoptr(FuPlugin) plugin = fu_plugin_new(ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRFUNC);
	g_autoptr(GError) error = NULL;

	ret = fu_plugin_runner_backend_device_changed(plugin, device, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	fu_device_set_specialized_gtype(device, FU_TYPE_DEVICE);
	fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_ONLY_SUPPORTED);
	ret = fu_plugin_runner_backend_device_added(plugin, device, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_test_plugin_backend_proxy_device_added_cb(FuPlugin *plugin, FuDevice *device, gpointer user_data)
{
	FuDevice **dev = (FuDevice **)user_data;
	*dev = g_object_ref(device);
}

static void
fu_plugin_backend_proxy_device_func(void)
{
	gboolean ret;
	FuDevice *proxy;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuDevice) device = fu_device_new(ctx);
	g_autoptr(FuDevice) device_new = NULL;
	g_autoptr(FuPlugin) plugin = fu_plugin_new(ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRFUNC);
	g_autoptr(GError) error = NULL;

	fu_device_set_id(device, "testdev");
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_EMULATED);
	ret = fu_plugin_runner_backend_device_changed(plugin, device, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* watch for the new superclassed device */
	g_signal_connect(plugin,
			 "device-added",
			 G_CALLBACK(fu_test_plugin_backend_proxy_device_added_cb),
			 &device_new);

	fu_device_set_specialized_gtype(device, FU_TYPE_DEVICE);
	fu_device_set_proxy_gtype(device, FU_TYPE_SELF_TEST_DEVICE);
	ret = fu_plugin_runner_backend_device_added(plugin, device, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* check device was constructed */
	g_assert_nonnull(device_new);
	g_assert_true(FU_IS_DEVICE(device_new));

	/* check proxy was constructed */
	proxy = fu_device_get_proxy(device_new, &error);
	g_assert_no_error(error);
	g_assert_nonnull(proxy);
	g_assert_true(FU_IS_SELF_TEST_DEVICE(proxy));
}

static void
fu_plugin_quirks_device_func(void)
{
	FuDevice *device_tmp;
	GPtrArray *children;
	gboolean ret;
	g_autoptr(FuDevice) child1 = NULL;
	g_autoptr(FuDevice) child2 = NULL;
	g_autoptr(FuDevice) device = fu_device_new(NULL);
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(GError) error = NULL;

	ret = fu_context_load_quirks(ctx, FU_QUIRKS_LOAD_FLAG_NO_CACHE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* use quirk file to set device attributes */
	fu_device_set_physical_id(device, "usb:00:05");
	fu_device_set_context(device, ctx);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_instance_id_full(device,
				       "USB\\VID_0BDA&PID_1100",
				       FU_DEVICE_INSTANCE_FLAG_GENERIC |
					   FU_DEVICE_INSTANCE_FLAG_QUIRKS);
	fu_device_add_instance_id(device, "USB\\VID_0BDA&PID_1100&CID_1234");
	g_assert_cmpstr(fu_device_get_name(device), ==, "Hub");

	/* ensure the non-customer-id instance ID is not available */
	g_assert_true(fu_device_has_instance_id(device,
						"USB\\VID_0BDA&PID_1100&CID_1234",
						FU_DEVICE_INSTANCE_FLAG_QUIRKS));
	g_assert_true(fu_device_has_instance_id(device,
						"USB\\VID_0BDA&PID_1100&CID_1234",
						FU_DEVICE_INSTANCE_FLAG_VISIBLE));
	g_assert_true(fu_device_has_instance_id(device,
						"USB\\VID_0BDA&PID_1100",
						FU_DEVICE_INSTANCE_FLAG_QUIRKS));
	g_assert_false(fu_device_has_instance_id(device,
						 "USB\\VID_0BDA&PID_1100",
						 FU_DEVICE_INSTANCE_FLAG_VISIBLE));

	/* ensure children are created */
	children = fu_device_get_children(device);
	g_assert_cmpint(children->len, ==, 1);
	device_tmp = g_ptr_array_index(children, 0);
	g_assert_cmpstr(fu_device_get_name(device_tmp), ==, "HDMI");
	g_assert_true(fu_device_has_flag(device_tmp, FWUPD_DEVICE_FLAG_UPDATABLE));

	/* get this one specific child */
	child1 = fu_device_get_child_by_logical_id(device, "USB\\VID_0763&PID_2806&I2C_01", &error);
	g_assert_no_error(error);
	g_assert_nonnull(child1);
	child2 = fu_device_get_child_by_logical_id(device, "SPI", &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_null(child2);
}

static gboolean
fu_device_poll_cb(FuDevice *device, GError **error)
{
	guint64 cnt = fu_device_get_metadata_integer(device, "cnt");
	g_debug("poll cnt=%" G_GUINT64_FORMAT, cnt);
	fu_device_set_metadata_integer(device, "cnt", cnt + 1);
	return TRUE;
}

static void
fu_device_poll_func(void)
{
	gboolean ret;
	g_autoptr(FuDevice) device = fu_device_new(NULL);
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(GError) error = NULL;
	FuDeviceClass *klass = FU_DEVICE_GET_CLASS(device);
	guint cnt;

	klass->poll = fu_device_poll_cb;
	fu_device_set_metadata_integer(device, "cnt", 0);

	/* manual poll */
	ret = fu_device_poll(device, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	cnt = fu_device_get_metadata_integer(device, "cnt");
	g_assert_cmpint(cnt, ==, 1);

	/* set up a 10ms poll */
	fu_device_set_poll_interval(device, 5);
	fu_test_loop_run_with_timeout(50);
	fu_test_loop_quit();
	cnt = fu_device_get_metadata_integer(device, "cnt");
	g_assert_cmpint(cnt, >=, 5);
	fu_test_loop_quit();

	/* auto pause */
	fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_AUTO_PAUSE_POLLING);
	locker = fu_device_poll_locker_new(device, &error);
	g_assert_no_error(error);
	g_assert_nonnull(locker);
	fu_test_loop_run_with_timeout(25);
	g_clear_object(&locker);
	g_assert_cmpint(fu_device_get_metadata_integer(device, "cnt"), ==, cnt);
	fu_test_loop_quit();

	/* disable the poll manually */
	fu_device_set_poll_interval(device, 0);
	fu_test_loop_run_with_timeout(25);
	fu_test_loop_quit();
	g_assert_cmpint(fu_device_get_metadata_integer(device, "cnt"), ==, cnt);
	fu_test_loop_quit();
}

static void
fu_device_func(void)
{
	g_autofree gchar *fn = NULL;
	g_autofree gchar *str = NULL;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuDevice) device = fu_device_new(ctx);
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) possible_plugins = NULL;

	/* only add one plugin name of the same type */
	fu_device_add_possible_plugin(device, "test");
	fu_device_add_possible_plugin(device, "test");
	possible_plugins = fu_device_get_possible_plugins(device);
	g_assert_cmpint(possible_plugins->len, ==, 1);

	fn = g_test_build_filename(G_TEST_DIST, "tests", "sys_vendor", NULL);
	str = fu_device_get_contents(device, fn, G_MAXSIZE, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpstr(str, ==, "FwupdTest\n");

	blob = fu_device_get_contents_bytes(device, fn, 5, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(g_bytes_get_size(blob), ==, 5);
}

static void
fu_device_vfuncs_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuDevice) device = fu_device_new(ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuFirmware) firmware_dummy = fu_firmware_new();
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;

	/* nop: error */
	ret = fu_device_get_results(device, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_false(ret);
	g_clear_error(&error);

	ret = fu_device_write_firmware(device,
				       firmware_dummy,
				       progress,
				       FWUPD_INSTALL_FLAG_NONE,
				       &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_false(ret);
	g_clear_error(&error);

	firmware = fu_device_read_firmware(device, progress, FU_FIRMWARE_PARSE_FLAG_NONE, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_null(firmware);
	g_clear_error(&error);

	blob = fu_device_dump_firmware(device, progress, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_null(blob);
	g_clear_error(&error);

	ret = fu_device_unbind_driver(device, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_false(ret);
	g_clear_error(&error);
	ret = fu_device_bind_driver(device, "subsystem", "driver", &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_false(ret);
	g_clear_error(&error);

	/* nop: ignore */
	ret = fu_device_detach(device, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_device_attach(device, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_device_activate(device, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* no-probe */
	fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_NO_PROBE);
	ret = fu_device_probe(device, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_false(ret);
	g_clear_error(&error);
}

static void
fu_device_instance_ids_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuDevice) device = fu_device_new(ctx);
	g_autoptr(GError) error = NULL;

	/* do not save silo */
	ret = fu_context_load_quirks(ctx, FU_QUIRKS_LOAD_FLAG_NO_CACHE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* sanity check */
	g_assert_false(fu_device_has_guid(device, "c0a26214-223b-572a-9477-cde897fe8619"));

	/* add a deferred instance ID that only gets converted on ->setup */
	fu_device_add_instance_id(device, "foobarbaz");
	g_assert_false(fu_device_has_guid(device, "c0a26214-223b-572a-9477-cde897fe8619"));

	ret = fu_device_setup(device, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_true(fu_device_has_guid(device, "c0a26214-223b-572a-9477-cde897fe8619"));

	/* this gets added immediately */
	fu_device_add_instance_id(device, "bazbarfoo");
	g_assert_true(fu_device_has_guid(device, "77e49bb0-2cd6-5faf-bcee-5b7fbe6e944d"));
}

static void
fu_device_composite_id_func(void)
{
	g_autoptr(FuDevice) dev1 = fu_device_new(NULL);
	g_autoptr(FuDevice) dev2 = fu_device_new(NULL);
	g_autoptr(FuDevice) dev3 = fu_device_new(NULL);
	g_autoptr(FuDevice) dev4 = fu_device_new(NULL);

	/* single device */
	fu_device_set_id(dev1, "dev1");
	g_assert_cmpstr(fu_device_get_composite_id(dev1),
			==,
			"3b42553c4e3241e8f3f8fbc19a69fa2f95708a9d");
	fu_device_set_id(dev2, "dev2");

	/* one child */
	fu_device_add_child(dev1, dev2);
	g_assert_cmpstr(fu_device_get_composite_id(dev1),
			==,
			"3b42553c4e3241e8f3f8fbc19a69fa2f95708a9d");
	g_assert_cmpstr(fu_device_get_composite_id(dev2),
			==,
			"3b42553c4e3241e8f3f8fbc19a69fa2f95708a9d");

	/* add a different "family" */
	fu_device_set_id(dev3, "dev3");
	fu_device_set_id(dev4, "dev4");
	fu_device_add_child(dev3, dev4);
	fu_device_add_child(dev2, dev3);
	g_assert_cmpstr(fu_device_get_composite_id(dev1),
			==,
			"3b42553c4e3241e8f3f8fbc19a69fa2f95708a9d");
	g_assert_cmpstr(fu_device_get_composite_id(dev2),
			==,
			"3b42553c4e3241e8f3f8fbc19a69fa2f95708a9d");
	g_assert_cmpstr(fu_device_get_composite_id(dev3),
			==,
			"3b42553c4e3241e8f3f8fbc19a69fa2f95708a9d");
	g_assert_cmpstr(fu_device_get_composite_id(dev4),
			==,
			"3b42553c4e3241e8f3f8fbc19a69fa2f95708a9d");

	/* change the parent ID */
	fu_device_set_id(dev1, "dev1-NEW");
	g_assert_cmpstr(fu_device_get_composite_id(dev1),
			==,
			"a4c8efc6a0a58c2dc14c05fd33186703f7352997");
	g_assert_cmpstr(fu_device_get_composite_id(dev2),
			==,
			"a4c8efc6a0a58c2dc14c05fd33186703f7352997");
}

static void
fu_device_inhibit_func(void)
{
	g_autoptr(FuDevice) device = fu_device_new(NULL);

	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_set_battery_threshold(device, 25);
	g_assert_true(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE));
	g_assert_false(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE_HIDDEN));

	/* does not exist -> fine */
	fu_device_uninhibit(device, "NOTGOINGTOEXIST");
	g_assert_false(fu_device_has_inhibit(device, "NOTGOINGTOEXIST"));

	/* first one */
	fu_device_inhibit(device, "needs-activation", "Device is pending activation");
	g_assert_true(fu_device_has_inhibit(device, "needs-activation"));
	g_assert_true(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE_HIDDEN));
	g_assert_false(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE));

	/* another */
	fu_device_set_battery_level(device, 5);
	g_assert_true(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE_HIDDEN));
	g_assert_false(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE));

	/* activated, power still too low */
	fu_device_uninhibit(device, "needs-activation");
	g_assert_false(fu_device_has_inhibit(device, "needs-activation"));
	g_assert_true(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE_HIDDEN));
	g_assert_false(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE));

	/* we got some more power -> fine */
	fu_device_set_battery_level(device, 95);
	g_assert_true(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE));
	g_assert_false(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE_HIDDEN));
}

static void
fu_device_inhibit_updateable_func(void)
{
	g_autoptr(FuDevice) device = fu_device_new(NULL);

	g_assert_false(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE));
	g_assert_false(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE_HIDDEN));
	g_assert_cmpstr(fu_device_get_update_error(device), ==, NULL);

	/* first one */
	fu_device_inhibit(device, "needs-activation", "Device is pending activation");
	g_assert_false(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE_HIDDEN));
	g_assert_false(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE));
	g_assert_cmpstr(fu_device_get_update_error(device), ==, "Device is pending activation");

	/* activated, but still not updatable */
	fu_device_uninhibit(device, "needs-activation");
	g_assert_false(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE));
	g_assert_false(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE_HIDDEN));
	g_assert_cmpstr(fu_device_get_update_error(device), ==, NULL);
}

static void
fu_device_custom_flags_func(void)
{
	g_autofree gchar *tmp = NULL;
	g_autoptr(FuDevice) device = fu_device_new(NULL);

	fu_device_register_private_flag(device, "foo");
	fu_device_register_private_flag(device, "bar");

	fu_device_set_custom_flags(device, "foo");
	g_assert_true(fu_device_has_private_flag(device, "foo"));
	fu_device_set_custom_flags(device, "bar");
	g_assert_true(fu_device_has_private_flag(device, "foo"));
	g_assert_true(fu_device_has_private_flag(device, "bar"));
	fu_device_set_custom_flags(device, "~bar");
	g_assert_true(fu_device_has_private_flag(device, "foo"));
	g_assert_false(fu_device_has_private_flag(device, "bar"));
	fu_device_set_custom_flags(device, "baz");
	g_assert_true(fu_device_has_private_flag(device, "foo"));
	g_assert_false(fu_device_has_private_flag(device, "bar"));

	tmp = fu_device_to_string(device);
	g_assert_cmpstr(tmp,
			==,
			"FuDevice:\n"
			"  Flags:                none\n"
			"  AcquiesceDelay:       50\n"
			"  CustomFlags:          baz\n"
			"  PrivateFlags:         foo\n");
}

static void
fu_device_flags_func(void)
{
	g_autoptr(FuDevice) device = fu_device_new(NULL);
	g_autoptr(FuDevice) proxy = fu_device_new(NULL);

	g_assert_cmpint(fu_device_get_flags(device), ==, FWUPD_DEVICE_FLAG_NONE);

	/* remove IS_BOOTLOADER if is a BOOTLOADER */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_NEEDS_BOOTLOADER);
	g_assert_cmpint(fu_device_get_flags(device), ==, FWUPD_DEVICE_FLAG_NEEDS_BOOTLOADER);
	fu_device_remove_flag(device, FWUPD_DEVICE_FLAG_NEEDS_BOOTLOADER);

	/* check implication */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);
	g_assert_cmpint(fu_device_get_flags(device),
			==,
			FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE | FWUPD_DEVICE_FLAG_CAN_VERIFY);
	fu_device_remove_flag(device,
			      FWUPD_DEVICE_FLAG_CAN_VERIFY | FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);

	/* negation */
	fu_device_set_custom_flags(device, "is-bootloader,updatable");
	g_assert_cmpint(fu_device_get_flags(device),
			==,
			FWUPD_DEVICE_FLAG_IS_BOOTLOADER | FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_set_custom_flags(device, "~is-bootloader");
	g_assert_cmpint(fu_device_get_flags(device), ==, FWUPD_DEVICE_FLAG_UPDATABLE);

	/* setting flags on the proxy should propagate to the device that *uses* the proxy */
	fu_device_set_proxy(device, proxy);
	fu_device_add_flag(proxy, FWUPD_DEVICE_FLAG_EMULATED);
	g_assert_true(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_EMULATED));

	/* unsetting flags on the proxy should unpropagate to the device that *uses* the proxy */
	fu_device_remove_flag(proxy, FWUPD_DEVICE_FLAG_EMULATED);
	g_assert_false(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_EMULATED));
}

static void
fu_device_children_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuDevice) child = fu_device_new(NULL);
	g_autoptr(FuDevice) parent = fu_device_new(ctx);
	g_autoptr(GError) error = NULL;

	/* do not save silo */
	ret = fu_context_load_quirks(ctx, FU_QUIRKS_LOAD_FLAG_NO_CACHE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	fu_device_set_physical_id(child, "dummy");
	fu_device_set_physical_id(parent, "dummy");

	/* set up family */
	fu_device_add_child(parent, child);

	/* set an instance ID that will be converted to a GUID when the parent
	 * calls ->setup */
	fu_device_add_instance_id(child, "foo");
	g_assert_false(fu_device_has_guid(child, "b84ed8ed-a7b1-502f-83f6-90132e68adef"));

	/* setup parent, which also calls setup on child too (and thus also
	 * converts the instance ID to a GUID) */
	ret = fu_device_setup(parent, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_true(fu_device_has_guid(child, "b84ed8ed-a7b1-502f-83f6-90132e68adef"));
}

static void
fu_device_parent_func(void)
{
	g_autoptr(FuDevice) child = fu_device_new(NULL);
	g_autoptr(FuDevice) child_root = NULL;
	g_autoptr(FuDevice) grandparent = fu_device_new(NULL);
	g_autoptr(FuDevice) grandparent_root = NULL;
	g_autoptr(FuDevice) parent = fu_device_new(NULL);
	g_autoptr(FuDevice) parent_root = NULL;

	fu_device_set_physical_id(child, "dummy");
	fu_device_set_physical_id(grandparent, "dummy");
	fu_device_set_physical_id(parent, "dummy");

	/* set up three layer family */
	fu_device_add_child(grandparent, parent);
	fu_device_add_child(parent, child);

	/* check parents */
	g_assert_true(fu_device_get_parent_internal(child) == parent);
	g_assert_true(fu_device_get_parent_internal(parent) == grandparent);
	g_assert_true(fu_device_get_parent_internal(grandparent) == NULL);

	/* check root */
	child_root = fu_device_get_root(child);
	g_assert_true(child_root == grandparent);
	parent_root = fu_device_get_root(parent);
	g_assert_true(parent_root == grandparent);
	grandparent_root = fu_device_get_root(child);
	g_assert_true(grandparent_root == grandparent);
}

static void
fu_device_incorporate_descendant_func(void)
{
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuDevice) device = fu_device_new(ctx);
	g_autoptr(FuSelfTestDevice) test_device = g_object_new(FU_TYPE_SELF_TEST_DEVICE, NULL);

	fu_device_set_name(device, "FuDevice");
	fu_device_set_summary(FU_DEVICE(test_device), "FuSelfTestDevice");

	fu_device_incorporate(FU_DEVICE(test_device), device, FU_DEVICE_INCORPORATE_FLAG_ALL);
	g_assert_cmpstr(fu_device_get_name(FU_DEVICE(test_device)), ==, "FuDevice");

	/* this won't explode as device_class->incorporate is checking types */
	fu_device_incorporate(device, FU_DEVICE(test_device), FU_DEVICE_INCORPORATE_FLAG_ALL);
	g_assert_cmpstr(fu_device_get_summary(device), ==, "FuSelfTestDevice");
}

static void
fu_device_incorporate_non_generic_func(void)
{
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuDevice) device = fu_device_new(ctx);
	g_autoptr(FuDevice) donor = fu_device_new(ctx);

	fu_device_add_instance_id_full(donor,
				       "USB\\VID_273F&PID_1004",
				       FU_DEVICE_INSTANCE_FLAG_GENERIC |
					   FU_DEVICE_INSTANCE_FLAG_VISIBLE);
	fu_device_add_instance_id_full(donor,
				       "USB\\VID_273F&PID_1004&CID_1234",
				       FU_DEVICE_INSTANCE_FLAG_VISIBLE);
	fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_NO_GENERIC_GUIDS);
	fu_device_incorporate(device, donor, FU_DEVICE_INCORPORATE_FLAG_INSTANCE_IDS);
	g_assert_false(fu_device_has_instance_id(device,
						 "USB\\VID_273F&PID_1004",
						 FU_DEVICE_INSTANCE_FLAG_VISIBLE));
	g_assert_true(fu_device_has_instance_id(device,
						"USB\\VID_273F&PID_1004&CID_1234",
						FU_DEVICE_INSTANCE_FLAG_VISIBLE));
	fu_device_convert_instance_ids(device);
	g_assert_false(fu_device_has_instance_id(device,
						 "USB\\VID_273F&PID_1004",
						 FU_DEVICE_INSTANCE_FLAG_VISIBLE));
	g_assert_true(fu_device_has_instance_id(device,
						"USB\\VID_273F&PID_1004&CID_1234",
						FU_DEVICE_INSTANCE_FLAG_VISIBLE));
	g_assert_false(
	    fwupd_device_has_instance_id(FWUPD_DEVICE(device), "USB\\VID_273F&PID_1004"));
	g_assert_true(
	    fwupd_device_has_instance_id(FWUPD_DEVICE(device), "USB\\VID_273F&PID_1004&CID_1234"));
}

static void
fu_device_incorporate_flag_func(void)
{
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuDevice) device = fu_device_new(ctx);
	g_autoptr(FuDevice) donor = fu_device_new(ctx);

	fu_device_set_logical_id(donor, "logi");
	fu_device_set_physical_id(donor, "phys");
	fu_device_add_vendor_id(donor, "PCI:0x1234");

	fu_device_incorporate(device,
			      donor,
			      FU_DEVICE_INCORPORATE_FLAG_VENDOR_IDS |
				  FU_DEVICE_INCORPORATE_FLAG_PHYSICAL_ID);
	g_assert_cmpstr(fu_device_get_physical_id(device), ==, "phys");
	g_assert_cmpstr(fu_device_get_logical_id(device), ==, NULL);
	g_assert_true(fu_device_has_vendor_id(device, "PCI:0x1234"));

	fu_device_incorporate(device, donor, FU_DEVICE_INCORPORATE_FLAG_ALL);
	g_assert_cmpstr(fu_device_get_logical_id(device), ==, "logi");
}

static void
fu_device_incorporate_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuDevice) device = fu_device_new(ctx);
	g_autoptr(FuDevice) donor = fu_device_new(ctx);
	g_autoptr(GError) error = NULL;

	/* load quirks */
	ret = fu_context_load_quirks(ctx, FU_QUIRKS_LOAD_FLAG_NO_CACHE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* set up donor device */
	fu_device_set_equivalent_id(donor, "0000000000000000000000000000000000000000");
	fu_device_set_metadata(donor, "test", "me");
	fu_device_set_metadata(donor, "test2", "me");
	fu_device_add_instance_str(donor, "VID", "0A5C");
	fu_device_add_instance_u16(donor, "PID", 0x6412);
	fu_device_add_instance_u32(donor, "BOARD_ID", 0x12345678);
	fu_device_register_private_flag(donor, "self-test");
	fu_device_add_private_flag(donor, "self-test");

	/* match a quirk entry, and then clear to ensure incorporate uses the quirk instance ID */
	ret = fu_device_build_instance_id_full(donor,
					       FU_DEVICE_INSTANCE_FLAG_QUIRKS,
					       &error,
					       "USB",
					       "VID",
					       "PID",
					       NULL);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpstr(fu_device_get_custom_flags(donor), ==, "ignore-runtime");
	fu_device_set_custom_flags(donor, "SHOULD_BE_REPLACED_WITH_QUIRK_VALUE");

	/* base properties */
	fu_device_add_flag(donor, FWUPD_DEVICE_FLAG_REQUIRE_AC);
	fu_device_set_created_usec(donor, 1514338000ull * G_USEC_PER_SEC);
	fu_device_set_modified_usec(donor, 1514338999ull * G_USEC_PER_SEC);
	fu_device_add_icon(donor, "computer");

	/* existing properties */
	fu_device_set_equivalent_id(device, "ffffffffffffffffffffffffffffffffffffffff");
	fu_device_set_metadata(device, "test2", "DO_NOT_OVERWRITE");
	fu_device_set_modified_usec(device, 1514340000ull * G_USEC_PER_SEC);

	/* incorporate properties from donor to device */
	fu_device_incorporate(device, donor, FU_DEVICE_INCORPORATE_FLAG_ALL);
	g_assert_cmpstr(fu_device_get_equivalent_id(device),
			==,
			"ffffffffffffffffffffffffffffffffffffffff");
	g_assert_cmpstr(fu_device_get_metadata(device, "test"), ==, "me");
	g_assert_cmpstr(fu_device_get_metadata(device, "test2"), ==, "DO_NOT_OVERWRITE");
	g_assert_true(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_REQUIRE_AC));
	g_assert_cmpint(fu_device_get_created_usec(device), ==, 1514338000ull * G_USEC_PER_SEC);
	g_assert_cmpint(fu_device_get_modified_usec(device), ==, 1514340000ull * G_USEC_PER_SEC);
	g_assert_cmpint(fu_device_get_icons(device)->len, ==, 1);
	ret = fu_device_build_instance_id(device, &error, "USB", "VID", NULL);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_true(
	    fu_device_has_instance_id(device, "USB\\VID_0A5C", FU_DEVICE_INSTANCE_FLAG_VISIBLE));
	g_assert_cmpstr(fu_device_get_custom_flags(device), ==, "ignore-runtime");
}

static void
fu_backend_emulate_count_cb(FuBackend *backend, FuDevice *device, gpointer user_data)
{
	guint *cnt = (guint *)user_data;
	(*cnt)++;
}

static void
fu_backend_emulate_func(void)
{
	gboolean ret;
	guint8 buf[] = {0x00, 0x00};
	guint added_cnt = 0;
	guint changed_cnt = 0;
	guint removed_cnt = 0;
	FuDevice *device;
	g_autofree gchar *json3 = NULL;
	g_autoptr(FuBackend) backend = NULL;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuDevice) device2 = NULL;
	g_autoptr(FuIoctl) ioctl = NULL;
	g_autoptr(GError) error = NULL;
	const gchar *json1 = "{\n"
			     "  \"UsbDevices\": [\n"
			     "    {\n"
			     "      \"Created\": \"2023-02-01T16:35:03.302027Z\",\n"
			     "      \"GType\": \"FuUdevDevice\",\n"
			     "      \"BackendId\": \"foo:bar:baz\",\n"
			     "      \"Events\": [\n"
			     "        {\n"
			     "          \"Id\": \"Ioctl:Request=0x007b,Data=AAA=,Length=0x2\",\n"
			     "          \"Data\": \"Aw==\",\n"
			     "          \"DataOut\": \"Aw==\"\n"
			     "        },\n"
			     "        {\n"
			     "          \"Id\": \"Ioctl:Request=0x007b,Data=AAA=,Length=0x2\",\n"
			     "          \"Data\": \"Aw==\",\n"
			     "          \"DataOut\": \"Aw==\"\n"
			     "        }\n"
			     "      ]\n"
			     "    }\n"
			     "  ]\n"
			     "}";
	const gchar *json2 = "{\n"
			     "  \"FwupdVersion\": \"" PACKAGE_VERSION "\",\n"
			     "  \"UsbDevices\": [\n"
			     "    {\n"
			     "      \"Created\": \"2099-02-01T16:35:03Z\",\n"
			     "      \"GType\": \"FuUdevDevice\",\n"
			     "      \"BackendId\": \"usb:FF:FF:06\"\n"
			     "    }\n"
			     "  ]\n"
			     "}";

	/* watch events */
	backend = g_object_new(FU_TYPE_BACKEND,
			       "context",
			       ctx,
			       "name",
			       "udev",
			       "device-gtype",
			       FU_TYPE_UDEV_DEVICE,
			       NULL);
	g_signal_connect(FU_BACKEND(backend),
			 "device-added",
			 G_CALLBACK(fu_backend_emulate_count_cb),
			 &added_cnt);
	g_signal_connect(FU_BACKEND(backend),
			 "device-removed",
			 G_CALLBACK(fu_backend_emulate_count_cb),
			 &removed_cnt);
	g_signal_connect(FU_BACKEND(backend),
			 "device-changed",
			 G_CALLBACK(fu_backend_emulate_count_cb),
			 &changed_cnt);

	/* parse */
	ret = fwupd_codec_from_json_string(FWUPD_CODEC(backend), json1, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(added_cnt, ==, 1);
	g_assert_cmpint(removed_cnt, ==, 0);
	g_assert_cmpint(changed_cnt, ==, 0);

	/* get device */
	device = fu_backend_lookup_by_id(backend, "foo:bar:baz");
	g_assert_no_error(error);
	g_assert_nonnull(device);
	g_assert_true(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_EMULATED));

#ifndef HAVE_IOCTL_H
	g_test_skip("no <ioctl.h> support");
	return;
#endif

	/* in-order */
	ioctl = fu_udev_device_ioctl_new(FU_UDEV_DEVICE(device));
	g_assert_nonnull(ioctl);
	ret = fu_ioctl_execute(ioctl, 123, buf, sizeof(buf), NULL, 0, FU_IOCTL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* in-order, repeat */
	buf[0] = 0x00;
	buf[1] = 0x00;
	ret = fu_ioctl_execute(ioctl, 123, buf, sizeof(buf), NULL, 0, FU_IOCTL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* out-of-order */
	buf[0] = 0x00;
	buf[1] = 0x00;
	ret = fu_ioctl_execute(ioctl, 123, buf, sizeof(buf), NULL, 0, FU_IOCTL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* load the same data */
	ret = fwupd_codec_from_json_string(FWUPD_CODEC(backend), json1, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(added_cnt, ==, 1);
	g_assert_cmpint(removed_cnt, ==, 0);
	g_assert_cmpint(changed_cnt, ==, 1);
	device = fu_backend_lookup_by_id(backend, "foo:bar:baz");
	g_assert_no_error(error);
	g_assert_nonnull(device);
	g_assert_true(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_EMULATED));

	/* load a different device */
	ret = fwupd_codec_from_json_string(FWUPD_CODEC(backend), json2, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(added_cnt, ==, 2);
	g_assert_cmpint(changed_cnt, ==, 1);
	g_assert_cmpint(removed_cnt, ==, 1);
	device = fu_backend_lookup_by_id(backend, "usb:FF:FF:06");
	g_assert_no_error(error);
	g_assert_nonnull(device);

	/* save to string */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_EMULATION_TAG);
	json3 = fwupd_codec_to_json_string(FWUPD_CODEC(backend), FWUPD_CODEC_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(json3);
	g_debug("%s", json3);
	ret = fu_test_compare_lines(json3, json2, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* missing event, new path */
	fu_device_set_fwupd_version(device, PACKAGE_VERSION);
	device2 = fu_device_get_backend_parent_with_subsystem(device, "usb", &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_null(device2);

	/* check version */
	g_assert_false(fu_device_check_fwupd_version(device, "5.0.0"));
	g_assert_true(fu_device_check_fwupd_version(device, "1.9.19"));
}

static void
fu_backend_func(void)
{
	FuDevice *dev;
	gboolean ret;
	g_autoptr(FuBackend) backend = g_object_new(FU_TYPE_BACKEND, NULL);
	g_autoptr(FuDevice) dev1 = fu_device_new(NULL);
	g_autoptr(FuDevice) dev2 = fu_device_new(NULL);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) devices = NULL;

	/* defaults */
	g_assert_null(fu_backend_get_name(backend));
	g_assert_true(fu_backend_get_enabled(backend));

	/* load */
	ret = fu_backend_setup(backend, FU_BACKEND_SETUP_FLAG_NONE, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_backend_coldplug(backend, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* add two devices, then remove one of them */
	fu_device_set_physical_id(dev1, "dev1");
	fu_backend_device_added(backend, dev1);
	fu_device_set_physical_id(dev2, "dev2");
	fu_backend_device_added(backend, dev2);
	fu_backend_device_changed(backend, dev2);
	fu_backend_device_removed(backend, dev2);

	dev = fu_backend_lookup_by_id(backend, "dev1");
	g_assert_nonnull(dev);
	g_assert_true(dev == dev1);

	/* should have been removed */
	dev = fu_backend_lookup_by_id(backend, "dev2");
	g_assert_null(dev);

	/* get linear array */
	devices = fu_backend_get_devices(backend);
	g_assert_nonnull(devices);
	g_assert_cmpint(devices->len, ==, 1);
	dev = g_ptr_array_index(devices, 0);
	g_assert_nonnull(dev);
	g_assert_true(dev == dev1);
}

static void
fu_efivar_func(void)
{
	gboolean ret;
	gsize sz = 0;
	guint32 attr = 0;
	guint64 total;
	g_autofree guint8 *data = NULL;
	g_autoptr(FuEfivars) efivars = fu_dummy_efivars_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) names = NULL;

	/* check supported */
	ret = fu_efivars_supported(efivars, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* check free space */
	total = fu_efivars_space_free(efivars, &error);
	g_assert_no_error(error);
	g_assert_cmpint(total, ==, 10240);

	/* write and read a key */
	ret = fu_efivars_set_data(efivars,
				  FU_EFIVARS_GUID_EFI_GLOBAL,
				  "Test",
				  (guint8 *)"1",
				  1,
				  FU_EFI_VARIABLE_ATTR_NON_VOLATILE |
				      FU_EFI_VARIABLE_ATTR_RUNTIME_ACCESS,
				  &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_efivars_get_data(efivars,
				  FU_EFIVARS_GUID_EFI_GLOBAL,
				  "Test",
				  &data,
				  &sz,
				  &attr,
				  &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(sz, ==, 1);
	g_assert_cmpint(attr,
			==,
			FU_EFI_VARIABLE_ATTR_NON_VOLATILE | FU_EFI_VARIABLE_ATTR_RUNTIME_ACCESS);
	g_assert_cmpint(data[0], ==, '1');

	/* check free space again */
	total = fu_efivars_space_free(efivars, &error);
	g_assert_no_error(error);
	g_assert_cmpint(total, ==, 10203);

	/* check existing keys */
	g_assert_false(fu_efivars_exists(efivars, FU_EFIVARS_GUID_EFI_GLOBAL, "NotGoingToExist"));
	g_assert_true(fu_efivars_exists(efivars, FU_EFIVARS_GUID_EFI_GLOBAL, "Test"));

	/* list a few keys */
	names = fu_efivars_get_names(efivars, FU_EFIVARS_GUID_EFI_GLOBAL, &error);
	g_assert_no_error(error);
	g_assert_nonnull(names);
	g_assert_cmpint(names->len, ==, 1);

	/* check we can get the space used */
	total = fu_efivars_space_used(efivars, &error);
	g_assert_no_error(error);
	g_assert_cmpint(total, >=, 0x10);

	/* delete single key */
	ret = fu_efivars_delete(efivars, FU_EFIVARS_GUID_EFI_GLOBAL, "Test", &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_false(fu_efivars_exists(efivars, FU_EFIVARS_GUID_EFI_GLOBAL, "Test"));
	g_assert_false(fu_efivars_delete(efivars, FU_EFIVARS_GUID_EFI_GLOBAL, "Test", NULL));

	/* delete multiple keys */
	ret = fu_efivars_set_data(efivars,
				  FU_EFIVARS_GUID_EFI_GLOBAL,
				  "Test1",
				  (guint8 *)"1",
				  1,
				  0,
				  &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_efivars_set_data(efivars,
				  FU_EFIVARS_GUID_EFI_GLOBAL,
				  "Test2",
				  (guint8 *)"1",
				  1,
				  0,
				  &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_efivars_delete_with_glob(efivars, FU_EFIVARS_GUID_EFI_GLOBAL, "Test*", &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_false(fu_efivars_exists(efivars, FU_EFIVARS_GUID_EFI_GLOBAL, "Test1"));
	g_assert_false(fu_efivars_exists(efivars, FU_EFIVARS_GUID_EFI_GLOBAL, "Test2"));

	/* check free space again */
	total = fu_efivars_space_free(efivars, &error);
	g_assert_no_error(error);
	g_assert_cmpint(total, ==, 10240);

	/* read a key that doesn't exist */
	ret = fu_efivars_get_data(efivars,
				  FU_EFIVARS_GUID_EFI_GLOBAL,
				  "NotGoingToExist",
				  NULL,
				  NULL,
				  NULL,
				  &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_false(ret);
}

static void
fu_efivar_boot_func(void)
{
	FuFirmware *firmware_tmp;
	gboolean ret;
	const gchar *tmpdir = g_getenv("FWUPD_LOCALSTATEDIR");
	guint16 idx = 0;
	g_autofree gchar *pefile_fn = g_build_filename(tmpdir, "grubx64.efi", NULL);
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuEfiLoadOption) loadopt2 = NULL;
	g_autoptr(FuVolume) volume = fu_volume_new_from_mount_path(tmpdir);
	g_autoptr(GArray) bootorder2 = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) entries = NULL;
	g_autoptr(GPtrArray) esp_files = NULL;
	FuEfivars *efivars = fu_context_get_efivars(ctx);

	/* set and get BootCurrent */
	ret = fu_efivars_set_boot_current(efivars, 0x0001, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_efivars_get_boot_current(efivars, &idx, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(idx, ==, 0x0001);

	/* set and get BootNext */
	ret = fu_efivars_set_boot_next(efivars, 0x0002, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_efivars_get_boot_next(efivars, &idx, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(idx, ==, 0x0002);

	/* set and get BootOrder */
	ret = fu_efivars_build_boot_order(efivars, &error, 0x0001, 0x0002, G_MAXUINT16);
	g_assert_no_error(error);
	g_assert_true(ret);
	bootorder2 = fu_efivars_get_boot_order(efivars, &error);
	g_assert_no_error(error);
	g_assert_nonnull(bootorder2);
	g_assert_cmpint(bootorder2->len, ==, 2);
	idx = g_array_index(bootorder2, guint16, 0);
	g_assert_cmpint(idx, ==, 0x0001);
	idx = g_array_index(bootorder2, guint16, 1);
	g_assert_cmpint(idx, ==, 0x0002);

	/* add a plausible ESP */
	fu_volume_set_partition_kind(volume, FU_VOLUME_KIND_ESP);
	fu_volume_set_partition_uuid(volume, "41f5e9b7-eb4f-5c65-b8a6-f94b0ad54815");
	fu_context_add_esp_volume(ctx, volume);

	/* create Boot0001 and Boot0002 */
	ret = fu_efivars_create_boot_entry_for_volume(efivars,
						      0x0001,
						      volume,
						      "Fedora",
						      "grubx64.efi",
						      &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_efivars_create_boot_entry_for_volume(efivars,
						      0x0002,
						      volume,
						      "Firmware Update",
						      "fwupdx64.efi",
						      &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* check BootXXXX exists */
	loadopt2 = fu_efivars_get_boot_entry(efivars, 0x0001, &error);
	g_assert_no_error(error);
	g_assert_nonnull(loadopt2);
	entries = fu_efivars_get_boot_entries(efivars, &error);
	g_assert_no_error(error);
	g_assert_nonnull(bootorder2);
	g_assert_cmpint(bootorder2->len, ==, 2);

	/* check we detected something */
	esp_files =
	    fu_context_get_esp_files(ctx, FU_CONTEXT_ESP_FILE_FLAG_INCLUDE_FIRST_STAGE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(esp_files);
	g_assert_cmpint(esp_files->len, ==, 2);
	firmware_tmp = g_ptr_array_index(esp_files, 0);
	g_assert_cmpstr(fu_firmware_get_filename(firmware_tmp), ==, pefile_fn);
}

typedef struct {
	guint cnt_success;
	guint cnt_failed;
} FuDeviceRetryHelper;

static gboolean
fu_device_retry_success_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuDeviceRetryHelper *helper = (FuDeviceRetryHelper *)user_data;
	helper->cnt_success++;
	return TRUE;
}

static gboolean
fu_device_retry_failed_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuDeviceRetryHelper *helper = (FuDeviceRetryHelper *)user_data;
	helper->cnt_failed++;
	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "failed");
	return FALSE;
}

static gboolean
fu_device_retry_success_3rd_try_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuDeviceRetryHelper *helper = (FuDeviceRetryHelper *)user_data;
	if (helper->cnt_failed == 2) {
		helper->cnt_success++;
		return TRUE;
	}
	helper->cnt_failed++;
	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "failed");
	return FALSE;
}

static void
fu_device_retry_success_func(void)
{
	gboolean ret;
	g_autoptr(FuDevice) device = fu_device_new(NULL);
	g_autoptr(GError) error = NULL;
	FuDeviceRetryHelper helper = {
	    .cnt_success = 0,
	    .cnt_failed = 0,
	};
	fu_device_retry_add_recovery(device,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     fu_device_retry_failed_cb);
	ret = fu_device_retry(device, fu_device_retry_success_cb, 3, &helper, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(helper.cnt_success, ==, 1);
	g_assert_cmpint(helper.cnt_failed, ==, 0);
}

static void
fu_device_retry_failed_func(void)
{
	gboolean ret;
	g_autoptr(FuDevice) device = fu_device_new(NULL);
	g_autoptr(GError) error = NULL;
	FuDeviceRetryHelper helper = {
	    .cnt_success = 0,
	    .cnt_failed = 0,
	};
	fu_device_retry_add_recovery(device,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     fu_device_retry_success_cb);
	ret = fu_device_retry(device, fu_device_retry_failed_cb, 3, &helper, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL);
	g_assert_true(!ret);
	g_assert_cmpint(helper.cnt_success, ==, 2); /* do not reset for the last failure */
	g_assert_cmpint(helper.cnt_failed, ==, 3);
}

static void
fu_device_retry_hardware_func(void)
{
	gboolean ret;
	g_autoptr(FuDevice) device = fu_device_new(NULL);
	g_autoptr(GError) error = NULL;
	FuDeviceRetryHelper helper = {
	    .cnt_success = 0,
	    .cnt_failed = 0,
	};
	ret = fu_device_retry(device, fu_device_retry_success_3rd_try_cb, 3, &helper, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(helper.cnt_success, ==, 1);
	g_assert_cmpint(helper.cnt_failed, ==, 2);
}

static void
fu_device_possible_plugin_func(void)
{
	gboolean ret;
	g_autoptr(FuDevice) device = fu_device_new(NULL);
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) possible_plugins = NULL;

	ret = fu_device_set_quirk_kv(device, "Plugin", "dfu", FU_CONTEXT_QUIRK_SOURCE_FILE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* duplicate */
	ret = fu_device_set_quirk_kv(device, "Plugin", "dfu", FU_CONTEXT_QUIRK_SOURCE_FILE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* something else */
	ret = fu_device_set_quirk_kv(device, "Plugin", "abc", FU_CONTEXT_QUIRK_SOURCE_FILE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* remove the other thing */
	ret =
	    fu_device_set_quirk_kv(device, "Plugin", "~dfu", FU_CONTEXT_QUIRK_SOURCE_FILE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* verify */
	possible_plugins = fu_device_get_possible_plugins(device);
	g_assert_cmpint(possible_plugins->len, ==, 1);
	g_assert_cmpstr(g_ptr_array_index(possible_plugins, 0), ==, "abc");
}

static void
fu_device_parent_name_prefix_func(void)
{
	g_autoptr(FuDevice) device = fu_device_new(NULL);
	g_autoptr(FuDevice) parent = fu_device_new(NULL);

	fu_device_set_id(parent, "0000000000000000000000000000000000000000");
	fu_device_set_name(parent, "Parent1");

	fu_device_set_id(device, "1111111111111111111111111111111111111111");
	fu_device_set_name(device, "Child1");
	fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_PARENT_NAME_PREFIX);
	fu_device_set_parent(device, parent);

	g_assert_cmpstr(fu_device_get_name(parent), ==, "Parent1");
	g_assert_cmpstr(fu_device_get_name(device), ==, "Parent1 (Child1)");

	/* still set, change child */
	g_assert_true(
	    fu_device_has_private_flag(device, FU_DEVICE_PRIVATE_FLAG_PARENT_NAME_PREFIX));
	fu_device_set_name(device, "Child2");
	g_assert_cmpstr(fu_device_get_name(device), ==, "Parent1 (Child2)");
}

static void
fu_device_id_display_func(void)
{
	g_autoptr(FuDevice) device = fu_device_new(NULL);
	g_autofree gchar *id1 = NULL;
	g_autofree gchar *id2 = NULL;
	g_autofree gchar *id3 = NULL;
	g_autofree gchar *id4 = NULL;

	id1 = fu_device_get_id_display(device);
	g_assert_cmpstr(id1, ==, NULL);

	fu_device_set_id(device, "362301da643102b9f38477387e2193e57abaa590");
	id2 = fu_device_get_id_display(device);
	g_assert_cmpstr(id2, ==, "362301da643102b9f38477387e2193e57abaa590");

	fu_device_set_plugin(device, "uefi_dbx");
	id3 = fu_device_get_id_display(device);
	g_assert_cmpstr(id3, ==, "362301da643102b9f38477387e2193e57abaa590 {uefi_dbx}");

	fu_device_set_name(device, "UEFI dbx");
	id4 = fu_device_get_id_display(device);
	g_assert_cmpstr(id4, ==, "362301da643102b9f38477387e2193e57abaa590 [UEFI dbx]");
}

static void
fu_device_udev_func(void)
{
	g_autofree gchar *prop = NULL;
	g_autofree gchar *sysfs_path = g_test_build_filename(G_TEST_DIST, "tests", NULL);
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuUdevDevice) udev_device = fu_udev_device_new(ctx, sysfs_path);
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) attrs = NULL;

	prop = fu_udev_device_read_property(udev_device, "MODALIAS", &error);
	g_assert_no_error(error);
	g_assert_cmpstr(prop, ==, "hdaudio:v10EC0298r00100103a01");

	/* list all the files in the directory */
	attrs = fu_udev_device_list_sysfs(udev_device, &error);
	g_assert_no_error(error);
	g_assert_nonnull(attrs);
	g_assert_cmpint(attrs->len, >, 10);
}

int
main(int argc, char **argv)
{
	g_autofree gchar *testdatadir = NULL;
	g_autoptr(FuContext) ctx = fu_context_new();

	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);

	/* only critical and error are fatal */
	g_log_set_fatal_mask(NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);
	(void)g_setenv("G_MESSAGES_DEBUG", "all", TRUE);

	testdatadir = g_test_build_filename(G_TEST_DIST, "tests", NULL);
	(void)g_setenv("FWUPD_DATADIR", testdatadir, TRUE);
	(void)g_setenv("FWUPD_DATADIR_VENDOR_IDS", testdatadir, TRUE);
	(void)g_setenv("FWUPD_LIBDIR_PKG", testdatadir, TRUE);
	(void)g_setenv("FWUPD_SYSCONFDIR", testdatadir, TRUE);
	(void)g_setenv("FWUPD_SYSFSFWDIR", testdatadir, TRUE);
	(void)g_setenv("FWUPD_SYSFSFWATTRIBDIR", testdatadir, TRUE);
	(void)g_setenv("FWUPD_SYSFSDMIDIR", testdatadir, TRUE);
	(void)g_setenv("FWUPD_PROCFS", testdatadir, TRUE);
	(void)g_setenv("FWUPD_LOCALSTATEDIR", "/tmp/fwupd-self-test/var", TRUE);
	(void)g_setenv("FWUPD_PROFILE", "1", TRUE);
	(void)g_setenv("FWUPD_EFIVARS", "dummy", TRUE);
	(void)g_setenv("CACHE_DIRECTORY", "/tmp/fwupd-self-test/cache", TRUE);

	/* register all the GTypes manually */
	fu_context_add_firmware_gtypes(ctx);
	g_type_ensure(FU_TYPE_USB_BOS_DESCRIPTOR);

	g_test_add_func("/fwupd/plugin{quirks-append}", fu_plugin_quirks_append_func);
	g_test_add_func("/fwupd/quirks{vendor-ids}", fu_quirks_vendor_ids_func);
	g_test_add_func("/fwupd/common{olson-timezone-id}", fu_common_olson_timezone_id_func);
	g_test_add_func("/fwupd/config", fu_config_func);
	g_test_add_func("/fwupd/plugin", fu_plugin_func);
	g_test_add_func("/fwupd/plugin{vfuncs}", fu_plugin_vfuncs_func);
	g_test_add_func("/fwupd/plugin{device-gtype}", fu_plugin_device_gtype_func);
	g_test_add_func("/fwupd/plugin{backend-device}", fu_plugin_backend_device_func);
	g_test_add_func("/fwupd/plugin{backend-proxy-device}", fu_plugin_backend_proxy_device_func);
	g_test_add_func("/fwupd/plugin{config}", fu_plugin_config_func);
	g_test_add_func("/fwupd/plugin{devices}", fu_plugin_devices_func);
	g_test_add_func("/fwupd/plugin{device-inhibit-children}",
			fu_plugin_device_inhibit_children_func);
	g_test_add_func("/fwupd/plugin{delay}", fu_plugin_delay_func);
	g_test_add_func("/fwupd/plugin{quirks}", fu_plugin_quirks_func);
	g_test_add_func("/fwupd/plugin{fdt}", fu_plugin_fdt_func);
	g_test_add_func("/fwupd/plugin{quirks-performance}", fu_plugin_quirks_performance_func);
	g_test_add_func("/fwupd/plugin{quirks-device}", fu_plugin_quirks_device_func);
	g_test_add_func("/fwupd/backend", fu_backend_func);
	g_test_add_func("/fwupd/backend{emulate}", fu_backend_emulate_func);
	g_test_add_func("/fwupd/common{string-append-kv}", fu_string_append_func);
	g_test_add_func("/fwupd/common{strsafe}", fu_strsafe_func);
	g_test_add_func("/fwupd/common{cpuid}", fu_cpuid_func);
	g_test_add_func("/fwupd/efivar", fu_efivar_func);
	g_test_add_func("/fwupd/efivar{bootxxxx}", fu_efivar_boot_func);
	g_test_add_func("/fwupd/hwids", fu_hwids_func);
	g_test_add_func("/fwupd/context{flags}", fu_context_flags_func);
	g_test_add_func("/fwupd/context{backends}", fu_context_backends_func);
	g_test_add_func("/fwupd/context{efivars}", fu_context_efivars_func);
	g_test_add_func("/fwupd/context{hwids-dmi}", fu_context_hwids_dmi_func);
	g_test_add_func("/fwupd/context{hwids-unset}", fu_context_hwids_unset_func);
	g_test_add_func("/fwupd/context{hwids-fdt}", fu_context_hwids_fdt_func);
	g_test_add_func("/fwupd/context{firmware-gtypes}", fu_context_firmware_gtypes_func);
	g_test_add_func("/fwupd/context{state}", fu_context_state_func);
	g_test_add_func("/fwupd/context{udev-subsystems}", fu_context_udev_subsystems_func);
	g_test_add_func("/fwupd/smbios", fu_smbios_func);
	g_test_add_func("/fwupd/smbios3", fu_smbios3_func);
	g_test_add_func("/fwupd/archive{invalid}", fu_archive_invalid_func);
	g_test_add_func("/fwupd/archive{cab}", fu_archive_cab_func);
	g_test_add_func("/fwupd/device", fu_device_func);
	g_test_add_func("/fwupd/device{parent-name-prefix}", fu_device_parent_name_prefix_func);
	g_test_add_func("/fwupd/device{id-for-display}", fu_device_id_display_func);
	g_test_add_func("/fwupd/device{possible-plugin}", fu_device_possible_plugin_func);
	g_test_add_func("/fwupd/device{udev}", fu_device_udev_func);
	g_test_add_func("/fwupd/device{vfuncs}", fu_device_vfuncs_func);
	g_test_add_func("/fwupd/device{instance-ids}", fu_device_instance_ids_func);
	g_test_add_func("/fwupd/device{composite-id}", fu_device_composite_id_func);
	g_test_add_func("/fwupd/device{flags}", fu_device_flags_func);
	g_test_add_func("/fwupd/device{private-flags}", fu_device_custom_flags_func);
	g_test_add_func("/fwupd/device{inhibit}", fu_device_inhibit_func);
	g_test_add_func("/fwupd/device{inhibit-updateable}", fu_device_inhibit_updateable_func);
	g_test_add_func("/fwupd/device{parent}", fu_device_parent_func);
	g_test_add_func("/fwupd/device{children}", fu_device_children_func);
	g_test_add_func("/fwupd/device{incorporate}", fu_device_incorporate_func);
	g_test_add_func("/fwupd/device{incorporate-flag}", fu_device_incorporate_flag_func);
	g_test_add_func("/fwupd/device{incorporate-non-generic}",
			fu_device_incorporate_non_generic_func);
	g_test_add_func("/fwupd/device{incorporate-descendant}",
			fu_device_incorporate_descendant_func);
	if (g_test_slow())
		g_test_add_func("/fwupd/device{poll}", fu_device_poll_func);
	g_test_add_func("/fwupd/device{name}", fu_device_name_func);
	g_test_add_func("/fwupd/device{rescan}", fu_device_rescan_func);
	g_test_add_func("/fwupd/device{metadata}", fu_device_metadata_func);
	g_test_add_func("/fwupd/device{open-refcount}", fu_device_open_refcount_func);
	g_test_add_func("/fwupd/device{version-format}", fu_device_version_format_func);
	g_test_add_func("/fwupd/device{version-format-raw}", fu_device_version_format_raw_func);
	g_test_add_func("/fwupd/device{retry-success}", fu_device_retry_success_func);
	g_test_add_func("/fwupd/device{retry-failed}", fu_device_retry_failed_func);
	g_test_add_func("/fwupd/device{retry-hardware}", fu_device_retry_hardware_func);
	g_test_add_func("/fwupd/device{cfi-device}", fu_device_cfi_device_func);
	g_test_add_func("/fwupd/device{progress}", fu_plugin_device_progress_func);
	return g_test_run();
}
