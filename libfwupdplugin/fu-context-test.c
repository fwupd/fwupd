/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-context-private.h"
#include "fu-test.h"

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
	g_autofree gchar *filename = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuFirmware) fdt_tmp = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;

	/* deleted on error */
	tmpdir = fu_temporary_directory_new("context", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);

	(void)g_setenv("FWUPD_LOCALSTATEDIR", fu_temporary_directory_get_path(tmpdir), TRUE);

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
	filename = fu_temporary_directory_build(tmpdir, "lib", "fwupd", "system.dtb", NULL);
	file = g_file_new_for_path(filename);
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
fu_context_hwids_func(void)
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
fu_context_quirks_func(void)
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

int
main(int argc, char **argv)
{
	g_autofree gchar *testdatadir = NULL;
	g_autoptr(FuContext) ctx = fu_context_new();

	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	fu_context_add_firmware_gtypes(ctx);
	testdatadir = g_test_build_filename(G_TEST_DIST, "tests", NULL);
	(void)g_setenv("FWUPD_DATADIR", testdatadir, TRUE);
	(void)g_setenv("FWUPD_SYSFSFWATTRIBDIR", testdatadir, TRUE);
	(void)g_setenv("FWUPD_SYSFSDMIDIR", testdatadir, TRUE);
	(void)g_setenv("FWUPD_EFIVARS", "dummy", TRUE);
	g_test_add_func("/fwupd/context/flags", fu_context_flags_func);
	g_test_add_func("/fwupd/context/backends", fu_context_backends_func);
	g_test_add_func("/fwupd/context/efivars", fu_context_efivars_func);
	g_test_add_func("/fwupd/context/hwids", fu_context_hwids_func);
	g_test_add_func("/fwupd/context/hwids/dmi", fu_context_hwids_dmi_func);
	g_test_add_func("/fwupd/context/hwids/unset", fu_context_hwids_unset_func);
	g_test_add_func("/fwupd/context/hwids/fdt", fu_context_hwids_fdt_func);
	g_test_add_func("/fwupd/context/firmware-gtypes", fu_context_firmware_gtypes_func);
	g_test_add_func("/fwupd/context/state", fu_context_state_func);
	g_test_add_func("/fwupd/context/udev-subsystems", fu_context_udev_subsystems_func);
	g_test_add_func("/fwupd/context/quirks", fu_context_quirks_func);
	return g_test_run();
}
