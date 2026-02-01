/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-context-private.h"

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

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/hwids", fu_hwids_func);
	return g_test_run();
}
