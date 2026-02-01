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
fu_context_efivars_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_DUMMY_EFIVARS);
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
	g_autofree gchar *testdatadir = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;
	gboolean ret;

	/* set up test harness */
	testdatadir = g_test_build_filename(G_TEST_DIST, "tests", NULL);
	fu_context_set_path(ctx, FU_PATH_KIND_SYSFSDIR_DMI, testdatadir);

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
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;
	gboolean ret;

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
	g_autofree gchar *fn = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuFirmware) fdt_tmp = NULL;
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GFile) file = NULL;

	fu_context_add_firmware_gtypes(ctx);

	/* set up test harness */
	tmpdir = fu_temporary_directory_new("hwids", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);
	fu_context_set_tmpdir(ctx, FU_PATH_KIND_LOCALSTATEDIR_PKG, tmpdir);

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
	fn = fu_temporary_directory_build(tmpdir, "system.dtb", NULL);
	file = g_file_new_for_path(fn);
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
	g_autofree gchar *localtime = NULL;
	g_autofree gchar *timezone_id = NULL;
	g_autoptr(FuPathStore) pstore = fu_path_store_new();
	g_autoptr(GError) error = NULL;

#ifdef HOST_MACHINE_SYSTEM_DARWIN
	g_test_skip("not supported on Darwin");
	return;
#endif

	/* set up test harness */
	localtime = g_test_build_filename(G_TEST_DIST, "tests", "localtime", NULL);
	fu_path_store_set_path(pstore, FU_PATH_KIND_LOCALTIME, localtime);

	timezone_id = fu_common_get_olson_timezone_id(pstore, &error);
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
	g_autofree gchar *testdatadir = g_test_build_filename(G_TEST_DIST, "tests", NULL);
	g_autoptr(FuPathStore) pstore = fu_path_store_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(GHashTable) cpu_attrs = NULL;

	fu_path_store_set_path(pstore, FU_PATH_KIND_PROCFS, testdatadir);
	cpu_attrs = fu_cpu_get_attrs(pstore, &error);
	g_assert_no_error(error);
	g_assert_nonnull(cpu_attrs);
	g_assert_cmpstr(g_hash_table_lookup(cpu_attrs, "vendor_id"), ==, "AuthenticAMD");
	g_assert_cmpstr(g_hash_table_lookup(cpu_attrs, "fpu_exception"), ==, "yes");
}

static void
fu_hwids_func(void)
{
	g_autofree gchar *full_path = NULL;
	g_autofree gchar *testdatadir = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
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

	/* set up test harness */
	testdatadir = g_test_build_filename(G_TEST_DIST, "tests", NULL);
	fu_context_set_path(ctx, FU_PATH_KIND_SYSFSDIR_FW, testdatadir);

	/* DMI */
	full_path = g_test_build_filename(G_TEST_DIST, "tests", "dmi", "tables", NULL);
	if (!g_file_test(full_path, G_FILE_TEST_IS_DIR)) {
		g_test_skip("no DMI tables found");
		return;
	}

	ret = fu_context_load_hwinfo(ctx, progress, FU_CONTEXT_HWID_FLAG_LOAD_SMBIOS, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	g_assert_cmpstr(fu_context_get_hwid_value(ctx, FU_HWIDS_KEY_MANUFACTURER), ==, "LENOVO");
	g_assert_cmpstr(fu_context_get_hwid_value(ctx, FU_HWIDS_KEY_ENCLOSURE_KIND), ==, "a");
	g_assert_cmpstr(fu_context_get_hwid_value(ctx, FU_HWIDS_KEY_FAMILY), ==, "ThinkPad T440s");
	g_assert_cmpstr(fu_context_get_hwid_value(ctx, FU_HWIDS_KEY_PRODUCT_NAME),
			==,
			"20ARS19C0C");
	g_assert_cmpstr(fu_context_get_hwid_value(ctx, FU_HWIDS_KEY_BIOS_VENDOR), ==, "LENOVO");
	g_assert_cmpstr(fu_context_get_hwid_value(ctx, FU_HWIDS_KEY_BIOS_VERSION),
			==,
			"GJET75WW (2.25 )");
	g_assert_cmpstr(fu_context_get_hwid_value(ctx, FU_HWIDS_KEY_BIOS_MAJOR_RELEASE), ==, "02");
	g_assert_cmpstr(fu_context_get_hwid_value(ctx, FU_HWIDS_KEY_BIOS_MINOR_RELEASE), ==, "19");
	g_assert_cmpstr(fu_context_get_hwid_value(ctx, FU_HWIDS_KEY_PRODUCT_SKU),
			==,
			"LENOVO_MT_20AR_BU_Think_FM_ThinkPad T440s");
	for (guint i = 0; guids[i].key != NULL; i++) {
		FuHwids *hwids = fu_context_get_hwids(ctx);
		g_autofree gchar *guid = fu_hwids_get_guid(hwids, guids[i].key, &error);
		g_assert_no_error(error);
		g_assert_cmpstr(guid, ==, guids[i].value);
	}
	for (guint i = 0; guids[i].key != NULL; i++)
		g_assert_true(fu_context_has_hwid_guid(ctx, guids[i].value));
}

static void
fu_test_plugin_device_added_cb(FuPlugin *plugin, FuDevice *device, gpointer user_data)
{
	FuDevice **dev = (FuDevice **)user_data;
	*dev = g_object_ref(device);
	fu_test_loop_quit();
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
	g_autofree gchar *sysconfdir_imu = NULL;
	g_autofree gchar *sysconfdir_mut = NULL;
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuPlugin) plugin = fu_plugin_new(ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;

#ifdef _WIN32
	/* the Windows file permission model is different than a simple octal value */
	g_test_skip("chmod not supported on Windows");
	return;
#endif

	/* set up test harness */
	tmpdir = fu_temporary_directory_new("config", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);
	sysconfdir_imu = fu_temporary_directory_build(tmpdir, "etc", "fwupd", NULL);
	fu_context_set_path(ctx, FU_PATH_KIND_SYSCONFDIR_PKG, sysconfdir_imu);
	sysconfdir_mut = fu_temporary_directory_build(tmpdir, "var", "etc", "fwupd", NULL);
	fu_context_set_path(ctx, FU_PATH_KIND_LOCALCONFDIR_PKG, sysconfdir_mut);

	fu_plugin_set_name(plugin, "test");
	fn =
	    fu_context_build_filename(ctx, &error, FU_PATH_KIND_SYSCONFDIR_PKG, "fwupd.conf", NULL);
	g_assert_no_error(error);
	g_assert_nonnull(fn);
	ret = fu_path_mkdir_parent(fn, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = g_file_set_contents(fn, "", -1, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* mutable file we'll be writing */
	fn_mut = g_build_filename(sysconfdir_mut, "fwupd.conf", NULL);
	g_assert_nonnull(fn_mut);
	ret = fu_path_mkdir_parent(fn_mut, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
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

static void
fu_plugin_quirks_func(void)
{
	const gchar *tmp;
	gboolean ret;
	g_autofree gchar *testdatadir = NULL;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(GError) error = NULL;

	/* set up test harness */
	testdatadir = g_test_build_filename(G_TEST_DIST, "tests", "quirks.d", NULL);
	fu_context_set_path(ctx, FU_PATH_KIND_DATADIR_QUIRKS, testdatadir);

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
	g_autofree gchar *testdatadir = NULL;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuQuirks) quirks = fu_quirks_new(ctx);
	g_autoptr(GTimer) timer = g_timer_new();
	g_autoptr(GError) error = NULL;
	const gchar *keys[] = {"Name", "Children", "Flags", NULL};

	/* set up test harness */
	testdatadir = g_test_build_filename(G_TEST_DIST, "tests", "quirks.d", NULL);
	fu_context_set_path(ctx, FU_PATH_KIND_DATADIR_QUIRKS, testdatadir);

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
fu_plugin_quirks_append_func(void)
{
	FuPluginQuirksAppendHelper helper = {0};
	gboolean ret;
	g_autofree gchar *testdatadir = NULL;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuQuirks) quirks = fu_quirks_new(ctx);
	g_autoptr(GError) error = NULL;

	/* set up test harness */
	testdatadir = g_test_build_filename(G_TEST_DIST, "tests", "quirks.d", NULL);
	fu_context_set_path(ctx, FU_PATH_KIND_DATADIR_QUIRKS, testdatadir);

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
	g_autofree gchar *testdatadir = NULL;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autofree gchar *guid1 = fwupd_guid_hash_string("PCI\\VEN_8086");
	g_autofree gchar *guid2 = fwupd_guid_hash_string("USB\\VID_8086");
	g_autofree gchar *guid3 = fwupd_guid_hash_string("PNP\\VID_ICO");
	g_autofree gchar *guid4 = fwupd_guid_hash_string("PCI\\VEN_8086&DEV_0007");
	g_autofree gchar *guid5 = fwupd_guid_hash_string("USB\\VID_8086&PID_0001");
	g_autoptr(FuQuirks) quirks = fu_quirks_new(ctx);
	g_autoptr(GError) error = NULL;
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;

	tmpdir = fu_temporary_directory_new("quirks-vids", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);

	/* set up test harness */
	testdatadir = g_test_build_filename(G_TEST_DIST, "tests", NULL);
	fu_context_set_path(ctx, FU_PATH_KIND_DATADIR_VENDOR_IDS, testdatadir);
	fu_context_set_tmpdir(ctx, FU_PATH_KIND_CACHEDIR_PKG, tmpdir);

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
	g_autofree gchar *testdatadir = NULL;
	g_autoptr(FuDevice) child1 = NULL;
	g_autoptr(FuDevice) child2 = NULL;
	g_autoptr(FuDevice) device = fu_device_new(NULL);
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(GError) error = NULL;

	/* set up test harness */
	testdatadir = g_test_build_filename(G_TEST_DIST, "tests", "quirks.d", NULL);
	fu_context_set_path(ctx, FU_PATH_KIND_DATADIR_QUIRKS, testdatadir);

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
fu_udev_device_func(void)
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
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/plugin/quirks-append", fu_plugin_quirks_append_func);
	g_test_add_func("/fwupd/quirks/vendor-ids", fu_quirks_vendor_ids_func);
	g_test_add_func("/fwupd/common/olson-timezone-id", fu_common_olson_timezone_id_func);
	g_test_add_func("/fwupd/plugin", fu_plugin_func);
	g_test_add_func("/fwupd/plugin/vfuncs", fu_plugin_vfuncs_func);
	g_test_add_func("/fwupd/plugin/device-gtype", fu_plugin_device_gtype_func);
	g_test_add_func("/fwupd/plugin/backend-device", fu_plugin_backend_device_func);
	g_test_add_func("/fwupd/plugin/backend-proxy-device", fu_plugin_backend_proxy_device_func);
	g_test_add_func("/fwupd/plugin/config", fu_plugin_config_func);
	g_test_add_func("/fwupd/plugin/devices", fu_plugin_devices_func);
	g_test_add_func("/fwupd/plugin/delay", fu_plugin_delay_func);
	g_test_add_func("/fwupd/plugin/quirks", fu_plugin_quirks_func);
	g_test_add_func("/fwupd/plugin/fdt", fu_plugin_fdt_func);
	g_test_add_func("/fwupd/plugin/quirks-performance", fu_plugin_quirks_performance_func);
	g_test_add_func("/fwupd/plugin/quirks-device", fu_plugin_quirks_device_func);
	g_test_add_func("/fwupd/backend", fu_backend_func);
	g_test_add_func("/fwupd/backend/emulate", fu_backend_emulate_func);
	g_test_add_func("/fwupd/common/cpuid", fu_cpuid_func);
	g_test_add_func("/fwupd/hwids", fu_hwids_func);
	g_test_add_func("/fwupd/context/flags", fu_context_flags_func);
	g_test_add_func("/fwupd/context/backends", fu_context_backends_func);
	g_test_add_func("/fwupd/context/efivars", fu_context_efivars_func);
	g_test_add_func("/fwupd/context/hwids-dmi", fu_context_hwids_dmi_func);
	g_test_add_func("/fwupd/context/hwids-unset", fu_context_hwids_unset_func);
	g_test_add_func("/fwupd/context/hwids-fdt", fu_context_hwids_fdt_func);
	g_test_add_func("/fwupd/context/firmware-gtypes", fu_context_firmware_gtypes_func);
	g_test_add_func("/fwupd/context/state", fu_context_state_func);
	g_test_add_func("/fwupd/context/udev-subsystems", fu_context_udev_subsystems_func);
	g_test_add_func("/fwupd/archive/invalid", fu_archive_invalid_func);
	g_test_add_func("/fwupd/archive/cab", fu_archive_cab_func);
	g_test_add_func("/fwupd/udev-device", fu_udev_device_func);
	return g_test_run();
}
